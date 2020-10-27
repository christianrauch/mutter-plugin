/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (c) 2008 Intel Corp.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "meta/display.h"

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <string.h>

#include "clutter/clutter.h"
#include "meta/meta-backend.h"
#include "meta/meta-background-actor.h"
#include "meta/meta-background-group.h"
#include "meta/meta-monitor-manager.h"
#include "meta/meta-plugin.h"
#include "meta/util.h"
#include "meta/window.h"

#define DESTROY_TIMEOUT   100
#define MINIMIZE_TIMEOUT  250
#define MAP_TIMEOUT       250
#define SWITCH_TIMEOUT    500

#define ACTOR_DATA_KEY "MCCP-Default-actor-data"
#define DISPLAY_TILE_PREVIEW_DATA_KEY "MCCP-Default-display-tile-preview-data"

#define META_TYPE_DEFAULT_PLUGIN            (meta_default_plugin_get_type ())
#define META_DEFAULT_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), META_TYPE_DEFAULT_PLUGIN, MetaDefaultPlugin))
#define META_DEFAULT_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  META_TYPE_DEFAULT_PLUGIN, MetaDefaultPluginClass))
#define META_IS_DEFAULT_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), META_DEFAULT_PLUGIN_TYPE))
#define META_IS_DEFAULT_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  META_TYPE_DEFAULT_PLUGIN))
#define META_DEFAULT_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  META_TYPE_DEFAULT_PLUGIN, MetaDefaultPluginClass))

typedef struct _MetaDefaultPlugin        MetaDefaultPlugin;
typedef struct _MetaDefaultPluginClass   MetaDefaultPluginClass;
typedef struct _MetaDefaultPluginPrivate MetaDefaultPluginPrivate;

struct _MetaDefaultPlugin
{
  MetaPlugin parent;

  MetaDefaultPluginPrivate *priv;
};

struct _MetaDefaultPluginClass
{
  MetaPluginClass parent_class;
};

static void start      (MetaPlugin      *plugin);

/*
 * Plugin private data that we store in the .plugin_private member.
 */
struct _MetaDefaultPluginPrivate
{
  /* Valid only when switch_workspace effect is in progress */
  ClutterTimeline       *tml_switch_workspace1;
  ClutterTimeline       *tml_switch_workspace2;
  ClutterActor          *desktop1;
  ClutterActor          *desktop2;

  ClutterActor          *background_group;

  MetaPluginInfo         info;
};

META_PLUGIN_DECLARE_WITH_CODE (MetaDefaultPlugin, meta_default_plugin,
                               G_ADD_PRIVATE_DYNAMIC (MetaDefaultPlugin));

/*
 * Per actor private data we attach to each actor.
 */
typedef struct _ActorPrivate
{
  ClutterActor *orig_parent;

  ClutterTimeline *tml_minimize;
  ClutterTimeline *tml_destroy;
  ClutterTimeline *tml_map;
} ActorPrivate;

/* callback data for when animations complete */
typedef struct
{
  ClutterActor *actor;
  MetaPlugin *plugin;
} EffectCompleteData;


typedef struct _DisplayTilePreview
{
  ClutterActor   *actor;

  GdkRGBA        *preview_color;

  MetaRectangle   tile_rect;
} DisplayTilePreview;

static void
meta_default_plugin_class_init (MetaDefaultPluginClass *klass)
{
  MetaPluginClass *plugin_class  = META_PLUGIN_CLASS (klass);

  plugin_class->start            = start;
}

static void
meta_default_plugin_init (MetaDefaultPlugin *self)
{
  MetaDefaultPluginPrivate *priv;

  self->priv = priv = meta_default_plugin_get_instance_private (self);

  priv->info.name        = "Default Effects";
  priv->info.version     = "0.1";
  priv->info.author      = "Intel Corp.";
  priv->info.license     = "GPL";
  priv->info.description = "This is an example of a plugin implementation.";
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     MetaPlugin         *plugin)
{
  MetaDefaultPlugin *self = META_DEFAULT_PLUGIN (plugin);
  MetaDisplay *display = meta_plugin_get_display (plugin);

  int i, n;
  GRand *rand = g_rand_new_with_seed (123456);

  clutter_actor_destroy_all_children (self->priv->background_group);

  n = meta_display_get_n_monitors (display);
  for (i = 0; i < n; i++)
    {
      MetaRectangle rect;
      ClutterActor *background_actor;
      MetaBackground *background;
      ClutterColor color;

      meta_display_get_monitor_geometry (display, i, &rect);

      background_actor = meta_background_actor_new (display, i);

      clutter_actor_set_position (background_actor, rect.x, rect.y);
      clutter_actor_set_size (background_actor, rect.width, rect.height);

      /* Don't use rand() here, mesa calls srand() internally when
         parsing the driconf XML, but it's nice if the colors are
         reproducible.
      */
      clutter_color_init (&color,
                          g_rand_int_range (rand, 0, 255),
                          g_rand_int_range (rand, 0, 255),
                          g_rand_int_range (rand, 0, 255),
                          255);

      background = meta_background_new (display);
      meta_background_set_color (background, &color);
      meta_background_actor_set_background (META_BACKGROUND_ACTOR (background_actor), background);
      g_object_unref (background);

      meta_background_actor_set_vignette (META_BACKGROUND_ACTOR (background_actor),
                                          TRUE,
                                          0.5,
                                          0.5);

      clutter_actor_add_child (self->priv->background_group, background_actor);
    }

  g_rand_free (rand);
}

static void
init_keymap (MetaDefaultPlugin *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GVariant) props = NULL;
  g_autofree char *x11_layout = NULL;
  g_autofree char *x11_options = NULL;
  g_autofree char *x11_variant = NULL;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.freedesktop.locale1",
                                         "/org/freedesktop/locale1",
                                         "org.freedesktop.DBus.Properties",
                                         NULL,
                                         &error);
  if (!proxy)
    {
      g_message ("Failed to acquire org.freedesktop.locale1 proxy: %s, "
                 "probably running in CI",
                 error->message);
      return;
    }

  result = g_dbus_proxy_call_sync (proxy,
                                   "GetAll",
                                   g_variant_new ("(s)",
                                                  "org.freedesktop.locale1"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   100,
                                   NULL,
                                   &error);
  if (!result)
    {
      g_warning ("Failed to retrieve locale properties: %s", error->message);
      return;
    }

  props = g_variant_get_child_value (result, 0);
  if (!props)
    {
      g_warning ("No locale properties found");
      return;
    }

  if (!g_variant_lookup (props, "X11Layout", "s", &x11_layout))
    x11_layout = g_strdup ("us");

  if (!g_variant_lookup (props, "X11Options", "s", &x11_options))
    x11_options = g_strdup ("");

  if (!g_variant_lookup (props, "X11Variant", "s", &x11_variant))
    x11_variant = g_strdup ("");

  meta_backend_set_keymap (meta_get_backend (),
                           x11_layout, x11_variant, x11_options);
}

static void
start (MetaPlugin *plugin)
{
  MetaDefaultPlugin *self = META_DEFAULT_PLUGIN (plugin);
  MetaDisplay *display = meta_plugin_get_display (plugin);
  MetaMonitorManager *monitor_manager = meta_monitor_manager_get ();

  self->priv->background_group = meta_background_group_new ();
  clutter_actor_insert_child_below (meta_get_window_group_for_display (display),
                                    self->priv->background_group, NULL);

  g_signal_connect (monitor_manager, "monitors-changed",
                    G_CALLBACK (on_monitors_changed), plugin);

  on_monitors_changed (monitor_manager, plugin);

  if (meta_is_wayland_compositor ())
    init_keymap (self);

  clutter_actor_show (meta_get_stage_for_display (display));

  meta_show_dialog ("--info", "Welcome to my custom plugin!", NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL);
}
