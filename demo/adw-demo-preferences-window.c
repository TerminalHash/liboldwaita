#include "adw-demo-preferences-window.h"

struct _AdwDemoPreferencesWindow
{
  AdwPreferencesWindow parent_instance;

  GtkWidget *subpage1;
  GtkWidget *subpage2;
};

G_DEFINE_TYPE (AdwDemoPreferencesWindow, adw_demo_preferences_window, ADW_TYPE_PREFERENCES_WINDOW)

AdwDemoPreferencesWindow *
adw_demo_preferences_window_new (void)
{
  return g_object_new (ADW_TYPE_DEMO_PREFERENCES_WINDOW, NULL);
}

static void
return_to_preferences_cb (AdwDemoPreferencesWindow *self)
{
  adw_preferences_window_close_subpage (ADW_PREFERENCES_WINDOW (self));
}

static void
subpage1_activated_cb (AdwDemoPreferencesWindow *self)
{
  adw_preferences_window_present_subpage (ADW_PREFERENCES_WINDOW (self), self->subpage1);
}

static void
subpage2_activated_cb (AdwDemoPreferencesWindow *self)
{
  adw_preferences_window_present_subpage (ADW_PREFERENCES_WINDOW (self), self->subpage2);
}

static void
toast_show_cb (AdwPreferencesWindow *window)
{
  adw_preferences_window_add_toast (window, adw_toast_new ("Example Toast"));
}

static void
adw_demo_preferences_window_class_init (AdwDemoPreferencesWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Adwaita1/Demo/ui/adw-demo-preferences-window.ui");

  gtk_widget_class_bind_template_child (widget_class, AdwDemoPreferencesWindow, subpage1);
  gtk_widget_class_bind_template_child (widget_class, AdwDemoPreferencesWindow, subpage2);

  gtk_widget_class_bind_template_callback (widget_class, return_to_preferences_cb);
  gtk_widget_class_bind_template_callback (widget_class, subpage1_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, subpage2_activated_cb);

  gtk_widget_class_install_action (widget_class, "toast.show", NULL, (GtkWidgetActionActivateFunc) toast_show_cb);
}

static void
adw_demo_preferences_window_init (AdwDemoPreferencesWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
