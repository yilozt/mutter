/* shell-blur-effect.h
 *
 * Copyright 2019 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

/*
 * have to rename the type, to avoide conflicts with gnome shell
 */ 

#include <clutter/clutter.h>

G_BEGIN_DECLS

/**
 * ShellBlurMode:
 * @SHELL_BLUR_MODE_ACTOR: blur the actor contents, and its children
 * @SHELL_BLUR_MODE_BACKGROUND: blur what's beneath the actor
 *
 * The mode of blurring of the effect.
 */
typedef enum
{
  SHELL_BLUR_MODE_ACTOR,
  SHELL_BLUR_MODE_BACKGROUND,
} ShellBlurMode;

#define META_SHELL_TYPE_BLUR_EFFECT (meta_shell_blur_effect_get_type())
G_DECLARE_FINAL_TYPE (MetaShellBlurEffect, meta_shell_blur_effect, META, SHELL_BLUR_EFFECT, ClutterEffect)

MetaShellBlurEffect *meta_shell_blur_effect_new (void);

int meta_shell_blur_effect_get_sigma (MetaShellBlurEffect *self);
void meta_shell_blur_effect_set_sigma (MetaShellBlurEffect *self,
                                       int              sigma);

float meta_shell_blur_effect_get_brightness (MetaShellBlurEffect *self);
void meta_shell_blur_effect_set_brightness (MetaShellBlurEffect *self,
                                            float            brightness);

ShellBlurMode meta_shell_blur_effect_get_mode (MetaShellBlurEffect *self);
void meta_shell_blur_effect_set_mode (MetaShellBlurEffect *self,
                                      ShellBlurMode    mode);

void meta_shell_blur_effect_set_skip (MetaShellBlurEffect *self,
                                      gboolean skip);

G_END_DECLS
