/*
 * Copyright (C) 2018 Endless, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-wayland.h"
#include "meta/meta-window-actor.h"
#include "wayland/meta-wayland-surface.h"
#include "meta/meta-shadow-factory.h"
#include "ui/frames.h"
#include "compositor/compositor-private.h"

struct _MetaWindowActorWayland
{
  MetaWindowActor parent;

  /* copy from meta-window-actor-x11.c */
  MetaShadow *focused_shadow;
  MetaShadow *unfocused_shadow;
  gboolean recompute_focused_shadow;
  gboolean recompute_unfocused_shadow;

  /* The region we should clip to when painting the shadow */
  cairo_region_t *shadow_clip;
  /* The frame region */
  cairo_region_t *frame_bounds;
  /* A region that matches the shape of the window, including frame bounds */
  cairo_region_t *shape_region;

  MetaWindowShape *shadow_shape;

  gboolean need_reshape;

  MetaShadowFactory *shadow_factory;
  gulong shadow_factory_changed_handler_id;
  gulong size_changed_id;
  gulong repaint_scheduled_id;
  
};

G_DEFINE_TYPE (MetaWindowActorWayland, meta_window_actor_wayland, META_TYPE_WINDOW_ACTOR)

typedef struct _SurfaceTreeTraverseData
{
  MetaWindowActor *window_actor;
  int index;
} SurfaceTreeTraverseData;

static gboolean
set_surface_actor_index (GNode    *node,
                         gpointer  data)
{
  MetaWaylandSurface *surface = node->data;
  MetaSurfaceActor *surface_actor = meta_wayland_surface_get_actor (surface);
  SurfaceTreeTraverseData *traverse_data = data;

  if (clutter_actor_contains (CLUTTER_ACTOR (traverse_data->window_actor),
                              CLUTTER_ACTOR (surface_actor)))
    {
      clutter_actor_set_child_at_index (
        CLUTTER_ACTOR (traverse_data->window_actor),
        CLUTTER_ACTOR (surface_actor),
        traverse_data->index);
    }
  else
    {
      clutter_actor_insert_child_at_index (
        CLUTTER_ACTOR (traverse_data->window_actor),
        CLUTTER_ACTOR (surface_actor),
        traverse_data->index);
    }
  traverse_data->index++;

  return FALSE;
}

void
meta_window_actor_wayland_rebuild_surface_tree (MetaWindowActor *actor)
{
  MetaSurfaceActor *surface_actor =
    meta_window_actor_get_surface (actor);
  MetaWaylandSurface *surface = meta_surface_actor_wayland_get_surface (
    META_SURFACE_ACTOR_WAYLAND (surface_actor));
  GNode *root_node = surface->subsurface_branch_node;
  SurfaceTreeTraverseData traverse_data;

  traverse_data = (SurfaceTreeTraverseData) {
    .window_actor = actor,
    .index = 0,
  };
  g_node_traverse (root_node,
                   G_IN_ORDER,
                   G_TRAVERSE_LEAVES,
                   -1,
                   set_surface_actor_index,
                   &traverse_data);
}

static void
surface_size_changed (MetaSurfaceActor *actor,
                      gpointer          user_data)
{
  MetaWindowActorWayland *actor_wayland = META_WINDOW_ACTOR_WAYLAND (user_data);
  actor_wayland->need_reshape = TRUE;
}

static void
surface_repaint_scheduled (MetaSurfaceActor *actor,
                           gpointer          user_data)
{
  MetaWindowActorWayland *actor_wayland = META_WINDOW_ACTOR_WAYLAND (user_data);
  MetaWindow *window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR(actor_wayland));
  meta_compositor_update_blur_behind( meta_display_get_compositor (window->display));
}


static void
meta_window_actor_wayland_assign_surface_actor (MetaWindowActor  *actor,
                                                MetaSurfaceActor *surface_actor)
{
  MetaWindowActorClass *parent_class =
    META_WINDOW_ACTOR_CLASS (meta_window_actor_wayland_parent_class);
  MetaSurfaceActor *prev_surface_actor;
  MetaWindowActorWayland *actor_wayland = META_WINDOW_ACTOR_WAYLAND (actor);


  g_warn_if_fail (!meta_window_actor_get_surface (actor));

  prev_surface_actor = meta_window_actor_get_surface (actor);
  if (prev_surface_actor)
    g_clear_signal_handler (&META_WINDOW_ACTOR_WAYLAND (actor)->size_changed_id,
                            prev_surface_actor);

  parent_class->assign_surface_actor (actor, surface_actor);

  meta_window_actor_wayland_rebuild_surface_tree (actor);

  actor_wayland->size_changed_id =
    g_signal_connect (surface_actor, "size-changed",
                      G_CALLBACK (surface_size_changed),
                      actor_wayland);

  actor_wayland->repaint_scheduled_id = 
    g_signal_connect (surface_actor, "repaint-scheduled",
                      G_CALLBACK (surface_repaint_scheduled),
                      actor_wayland);
}

static void
meta_window_actor_wayland_frame_complete (MetaWindowActor  *actor,
                                          ClutterFrameInfo *frame_info,
                                          int64_t           presentation_time)
{
}

static void
meta_window_actor_wayland_queue_frame_drawn (MetaWindowActor *actor,
                                             gboolean         skip_sync_delay)
{
}

static const char *
get_shadow_class (MetaWindowActorWayland *actor_x11)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_x11));
  MetaWindowType window_type;

  window_type = meta_window_get_window_type (window);
  switch (window_type)
    {
    case META_WINDOW_DROPDOWN_MENU:
    case META_WINDOW_COMBO:
      return "dropdown-menu";
    case META_WINDOW_POPUP_MENU:
      return "popup-menu";
    default:
      {
        MetaFrameType frame_type;

        frame_type = meta_window_get_frame_type (window);
        return meta_frame_type_to_string (frame_type);
      }
    }
}

static void
invalidate_shadow (MetaWindowActorWayland *actor_wayland)
{
  actor_wayland->recompute_focused_shadow = TRUE;
  actor_wayland->recompute_unfocused_shadow = TRUE;

  if (meta_window_actor_is_frozen (META_WINDOW_ACTOR (actor_wayland)))
    return;

  clutter_actor_queue_redraw (CLUTTER_ACTOR (actor_wayland));
  clutter_actor_invalidate_paint_volume (CLUTTER_ACTOR (actor_wayland));
}

static void
update_shape_region (MetaWindowActorWayland *actor_wayland)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_wayland));
  cairo_region_t *region = NULL;
  cairo_rectangle_int_t client_area;

  if (meta_window_actor_should_clip(META_WINDOW_ACTOR(actor_wayland)) && !window->frame)
    {
      meta_window_actor_get_corner_rect(META_WINDOW_ACTOR(actor_wayland), &client_area);
      region = cairo_region_create_rectangle(&client_area);  
    }
  else
    {
      return;
    }
  
  g_clear_pointer (&actor_wayland->shape_region, cairo_region_destroy);
  actor_wayland->shape_region = region;

  g_clear_pointer (&actor_wayland->shadow_shape, meta_window_shape_unref);

  invalidate_shadow (actor_wayland);
}

static cairo_region_t *
meta_window_get_clipped_frame_bounds(MetaWindow *window)
{
  g_return_val_if_fail(window, NULL);

  MetaWindowActor *actor = meta_window_actor_from_window(window);
  if (actor && !window->frame_bounds)
  {
    MetaRectangle rect;
    meta_window_actor_get_corner_rect(actor, &rect);
    window->frame_bounds = 
      meta_ui_frame_get_bounds_clipped(&rect,
                                       meta_prefs_get_round_corner_radius());
  }
  return window->frame_bounds;
}

static void
update_frame_bounds (MetaWindowActorWayland *actor_wayland)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_wayland));

  g_clear_pointer (&actor_wayland->frame_bounds, cairo_region_destroy);
  
  if (meta_window_actor_should_clip(META_WINDOW_ACTOR(actor_wayland)))
    actor_wayland->frame_bounds =
      cairo_region_copy(meta_window_get_clipped_frame_bounds(window));
}

static void
get_shape_bounds (MetaWindowActorWayland    *actor_wayland,
                  cairo_rectangle_int_t     *bounds)
{
  cairo_region_get_extents (actor_wayland->shape_region, bounds);
}

static void
check_needs_shadow (MetaWindowActorWayland *actor_wayland)
{
  MetaWindow *window =
    meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_wayland));
  MetaShadow *old_shadow = NULL;
  MetaShadow **shadow_location;
  gboolean recompute_shadow;
  gboolean should_have_shadow;
  gboolean appears_focused;

  should_have_shadow = 
    meta_window_actor_should_clip (META_WINDOW_ACTOR (actor_wayland));
  appears_focused = meta_window_appears_focused (window);

  if (appears_focused)
    {
      recompute_shadow = actor_wayland->recompute_focused_shadow;
      actor_wayland->recompute_focused_shadow = FALSE;
      shadow_location = &actor_wayland->focused_shadow;
    }
  else
    {
      recompute_shadow = actor_wayland->recompute_unfocused_shadow;
      actor_wayland->recompute_unfocused_shadow = FALSE;
      shadow_location = &actor_wayland->unfocused_shadow;
    }

  if (!should_have_shadow || recompute_shadow)
    {
      if (*shadow_location != NULL)
        {
          old_shadow = *shadow_location;
          *shadow_location = NULL;
        }
    }

  if (!*shadow_location && should_have_shadow)
    {
      MetaShadowFactory *factory = actor_wayland->shadow_factory;
      const char *shadow_class = get_shadow_class (actor_wayland);
      cairo_rectangle_int_t shape_bounds;

      if (!actor_wayland->shadow_shape)
        {
          actor_wayland->shadow_shape =
            meta_window_shape_new (actor_wayland->shape_region);
        }

      get_shape_bounds (actor_wayland, &shape_bounds);
      *shadow_location =
        meta_shadow_factory_get_shadow (factory,
                                        actor_wayland->shadow_shape,
                                        shape_bounds.width, shape_bounds.height,
                                        shadow_class, appears_focused);
    }

  if (old_shadow)
    meta_shadow_unref (old_shadow);
}

static void
get_shadow_params (MetaWindowActorWayland *actor_wayland,
                   gboolean                appears_focused,
                   MetaShadowParams       *params)
{
  const char *shadow_class = get_shadow_class (actor_wayland);

  meta_shadow_factory_get_params (actor_wayland->shadow_factory,
                                  shadow_class, appears_focused,
                                  params);
}

static gboolean
clip_shadow_under_window (MetaWindowActorWayland *actor_wayland)
{
  return TRUE;
}

static void
meta_window_actor_wayland_before_paint (MetaWindowActor  *actor,
                                        ClutterStageView *stage_view)
{
  MetaWindowActorWayland *actor_wayland = META_WINDOW_ACTOR_WAYLAND (actor);

  if (meta_window_actor_should_clip (actor))
    {
      update_frame_bounds (actor_wayland);
      if (actor_wayland->need_reshape)
      {
        update_shape_region (actor_wayland);
        actor_wayland->need_reshape = FALSE;
      }
      check_needs_shadow (actor_wayland);
    }
}

static void
get_shadow_bounds (MetaWindowActorWayland    *actor_x11,
                   gboolean                   appears_focused,
                   cairo_rectangle_int_t     *bounds)
{
  MetaShadow *shadow;
  cairo_rectangle_int_t shape_bounds;
  MetaShadowParams params;

  shadow = appears_focused ? actor_x11->focused_shadow
                           : actor_x11->unfocused_shadow;

  get_shape_bounds (actor_x11, &shape_bounds);
  get_shadow_params (actor_x11, appears_focused, &params);

  meta_shadow_get_bounds (shadow,
                          params.x_offset + shape_bounds.x,
                          params.y_offset + shape_bounds.y,
                          shape_bounds.width,
                          shape_bounds.height,
                          bounds);
}

/* Copy from ./meta-window-actor-x11.c
 * to draw shadows for rounded wayland clients
 * because oragin shadows has been cutted out
 */
static void
meta_window_actor_wayland_paint (ClutterActor        *actor,
                                 ClutterPaintContext *paint_context)
{
  MetaWindowActorWayland *actor_wayland = META_WINDOW_ACTOR_WAYLAND (actor);
  MetaWindow *window;
  gboolean appears_focused;
  MetaShadow *shadow;

  window = meta_window_actor_get_meta_window (META_WINDOW_ACTOR (actor_wayland));
  appears_focused = meta_window_appears_focused (window);
  shadow = appears_focused ? actor_wayland->focused_shadow
                           : actor_wayland->unfocused_shadow;

  if (shadow && meta_window_actor_should_clip (META_WINDOW_ACTOR (actor_wayland)))
    {
      MetaShadowParams params;
      cairo_rectangle_int_t shape_bounds;
      cairo_region_t *clip = actor_wayland->shadow_clip;
      CoglFramebuffer *framebuffer;

      meta_window_actor_get_corner_rect(META_WINDOW_ACTOR(actor_wayland), &shape_bounds);
      get_shadow_params (actor_wayland, appears_focused, &params);

      /* The frame bounds are already subtracted from actor_wayland->shadow_clip
       * if that exists.
       */
      if (!clip && clip_shadow_under_window (actor_wayland))
        {
          cairo_rectangle_int_t bounds;

          get_shadow_bounds (actor_wayland, appears_focused, &bounds);
          clip = cairo_region_create_rectangle (&bounds);

          if (actor_wayland->frame_bounds)
            cairo_region_subtract (clip, actor_wayland->frame_bounds);
        }

      framebuffer = clutter_paint_context_get_framebuffer (paint_context);
      meta_shadow_paint (shadow,
                         framebuffer,
                         params.x_offset + shape_bounds.x,
                         params.y_offset + shape_bounds.y,
                         shape_bounds.width,
                         shape_bounds.height,
                         (clutter_actor_get_paint_opacity (actor) *
                          params.opacity * window->opacity) / (255 * 255),
                         clip,
                         clip_shadow_under_window (actor_wayland));

      if (clip && clip != actor_wayland->shadow_clip)
        cairo_region_destroy (clip);
    }

  CLUTTER_ACTOR_CLASS (meta_window_actor_wayland_parent_class)->paint (actor,
                                                                       paint_context);
}

static void
meta_window_actor_wayland_after_paint (MetaWindowActor  *actor,
                                       ClutterStageView *stage_view)
{
}

static void
meta_window_actor_wayland_queue_destroy (MetaWindowActor *actor)
{
}

static void
meta_window_actor_wayland_set_frozen (MetaWindowActor *actor,
                                      gboolean         frozen)
{
}

static void
meta_window_actor_wayland_update_regions (MetaWindowActor *actor)
{
}

static gboolean
meta_window_actor_wayland_can_freeze_commits (MetaWindowActor *actor)
{
  return FALSE;
}

static void
meta_window_actor_wayland_constructed (GObject *object)
{
  MetaWindowActorWayland *actor_wayland = META_WINDOW_ACTOR_WAYLAND (object);

  /*
   * Start off with an empty shape region to maintain the invariant that it's
   * always set.
   */
  actor_wayland->shape_region = cairo_region_create ();

  G_OBJECT_CLASS (meta_window_actor_wayland_parent_class)->constructed (object);
}


static void
meta_window_actor_wayland_dispose (GObject *object)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (object);
  MetaSurfaceActor *surface_actor =
    meta_window_actor_get_surface (window_actor);
  MetaWindowActorWayland *actor_wayland = META_WINDOW_ACTOR_WAYLAND (object);
  
  g_autoptr (GList) children = NULL;
  GList *l;

  children = clutter_actor_get_children (CLUTTER_ACTOR (window_actor));
  for (l = children; l; l = l->next)
    {
      ClutterActor *child_actor = l->data;

      if (META_IS_SURFACE_ACTOR_WAYLAND (child_actor) &&
          child_actor != CLUTTER_ACTOR (surface_actor))
        clutter_actor_remove_child (CLUTTER_ACTOR (window_actor), child_actor);
    }
  
  g_clear_pointer (&actor_wayland->shape_region, cairo_region_destroy);
  g_clear_pointer (&actor_wayland->shadow_clip, cairo_region_destroy);
  g_clear_pointer (&actor_wayland->frame_bounds, cairo_region_destroy);

  g_clear_pointer (&actor_wayland->focused_shadow, meta_shadow_unref);
  g_clear_pointer (&actor_wayland->unfocused_shadow, meta_shadow_unref);
  g_clear_pointer (&actor_wayland->shadow_shape, meta_window_shape_unref);

  g_clear_signal_handler (&actor_wayland->size_changed_id, surface_actor);
  g_clear_signal_handler (&actor_wayland->repaint_scheduled_id, surface_actor);
  g_clear_signal_handler (&actor_wayland->shadow_factory_changed_handler_id,
                          actor_wayland->shadow_factory);
  G_OBJECT_CLASS (meta_window_actor_wayland_parent_class)->dispose (object);
}

static void
meta_window_actor_wayland_class_init (MetaWindowActorWaylandClass *klass)
{
  MetaWindowActorClass *window_actor_class = META_WINDOW_ACTOR_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  window_actor_class->assign_surface_actor = meta_window_actor_wayland_assign_surface_actor;
  window_actor_class->frame_complete = meta_window_actor_wayland_frame_complete;
  window_actor_class->queue_frame_drawn = meta_window_actor_wayland_queue_frame_drawn;
  window_actor_class->before_paint = meta_window_actor_wayland_before_paint;
  window_actor_class->after_paint = meta_window_actor_wayland_after_paint;
  window_actor_class->queue_destroy = meta_window_actor_wayland_queue_destroy;
  window_actor_class->set_frozen = meta_window_actor_wayland_set_frozen;
  window_actor_class->update_regions = meta_window_actor_wayland_update_regions;
  window_actor_class->can_freeze_commits = meta_window_actor_wayland_can_freeze_commits;

  actor_class->paint = meta_window_actor_wayland_paint;

  object_class->constructed = meta_window_actor_wayland_constructed;
  object_class->dispose = meta_window_actor_wayland_dispose;
}

static void
meta_window_actor_wayland_init (MetaWindowActorWayland *self)
{
  self->shadow_factory = meta_shadow_factory_get_default ();
  self->shadow_factory_changed_handler_id =
    g_signal_connect_swapped (self->shadow_factory,
                              "changed",
                              G_CALLBACK (invalidate_shadow),
                              self);
}
