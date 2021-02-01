/*
 * Copyright (C) 2019 Alexander Mikhaylenko <exalm7659@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#if !defined(_ADWAITA_INSIDE) && !defined(ADWAITA_COMPILATION)
#error "Only <adwaita.h> can be included directly."
#endif

#include "adw-swipe-tracker.h"

G_BEGIN_DECLS

#define ADW_SWIPE_BORDER 32

void adw_swipe_tracker_emit_begin_swipe (AdwSwipeTracker        *self,
                                         AdwNavigationDirection  direction,
                                         gboolean                direct);
void adw_swipe_tracker_emit_update_swipe (AdwSwipeTracker *self,
                                          double           progress);
void adw_swipe_tracker_emit_end_swipe (AdwSwipeTracker *self,
                                       gint64           duration,
                                       double           to);

G_END_DECLS