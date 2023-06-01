#include "adw-demo-page-split-views.h"

#include <glib/gi18n.h>

#include "adw-navigation-split-view-demo-window.h"

struct _AdwDemoPageSplitViews
{
  AdwBin parent_instance;
};

G_DEFINE_FINAL_TYPE (AdwDemoPageSplitViews, adw_demo_page_split_views, ADW_TYPE_BIN)

static void
demo_run_navigation_cb (AdwDemoPageSplitViews *self)
{
  AdwNavigationSplitViewDemoWindow *window = adw_navigation_split_view_demo_window_new ();
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));

  gtk_window_set_transient_for (GTK_WINDOW (window), GTK_WINDOW (root));
  gtk_window_present (GTK_WINDOW (window));
}

static void
adw_demo_page_split_views_class_init (AdwDemoPageSplitViewsClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Adwaita1/Demo/ui/pages/split-views/adw-demo-page-split-views.ui");

  gtk_widget_class_install_action (widget_class, "demo.run-navigation", NULL, (GtkWidgetActionActivateFunc) demo_run_navigation_cb);
}

static void
adw_demo_page_split_views_init (AdwDemoPageSplitViews *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
