/*
 * Copyright (C) 2020-2021 Purism SPC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Author: Alexander Mikhaylenko <alexander.mikhaylenko@puri.sm>
 */

#include "config.h"

#include "adw-tab-view-private.h"

#include "adw-gizmo-private.h"
#include "adw-widget-utils-private.h"

/* FIXME replace with groups */
static GSList *tab_view_list;

/**
 * AdwTabView:
 *
 * A dynamic tabbed container.
 *
 * `AdwTabView` is a container which shows one child at a time. While it
 * provides keyboard shortcuts for switching between pages, it does not provide
 * a visible tab bar and relies on external widgets for that, such as
 * [class@Adw.TabBar].
 *
 * `AdwTabView` maintains a [class@Adw.TabPage] object for each page, which
 * holds additional per-page properties. You can obtain the `AdwTabPage` for a
 * page with [method@Adw.TabView.get_page], and as the return value for
 * [method@Adw.TabView.append] and other functions for adding children.
 *
 * `AdwTabView` only aims to be useful for dynamic tabs in multi-window
 * document-based applications, such as web browsers, file managers, text
 * editors or terminals. It does not aim to replace [class@Gtk.Notebook] for use
 * cases such as tabbed dialogs.
 *
 * As such, it does not support disabling page reordering or detaching, or
 * adding children via [class@Gtk.Builder].
 *
 * ## CSS nodes
 *
 * `AdwTabView` has a main CSS node with the name `tabview`.
 *
 * Since: 1.0
 */

/**
 * AdwTabPage:
 *
 * An auxiliary class used by [class@Adw.TabView].
 */

struct _AdwTabPage
{
  GObject parent_instance;

  GtkWidget *child;
  AdwTabPage *parent;
  gboolean selected;
  gboolean pinned;
  char *title;
  char *tooltip;
  GIcon *icon;
  gboolean loading;
  GIcon *indicator_icon;
  gboolean indicator_activatable;
  gboolean needs_attention;

  gboolean closing;
};

G_DEFINE_FINAL_TYPE (AdwTabPage, adw_tab_page, G_TYPE_OBJECT)

enum {
  PAGE_PROP_0,
  PAGE_PROP_CHILD,
  PAGE_PROP_PARENT,
  PAGE_PROP_SELECTED,
  PAGE_PROP_PINNED,
  PAGE_PROP_TITLE,
  PAGE_PROP_TOOLTIP,
  PAGE_PROP_ICON,
  PAGE_PROP_LOADING,
  PAGE_PROP_INDICATOR_ICON,
  PAGE_PROP_INDICATOR_ACTIVATABLE,
  PAGE_PROP_NEEDS_ATTENTION,
  LAST_PAGE_PROP
};

static GParamSpec *page_props[LAST_PAGE_PROP];

struct _AdwTabView
{
  GtkWidget parent_instance;

  GtkStack *stack;
  GListStore *children;

  int n_pages;
  int n_pinned_pages;
  AdwTabPage *selected_page;
  GIcon *default_icon;
  GMenuModel *menu_model;

  int transfer_count;

  GtkWidget *shortcut_widget;
  GtkEventController *shortcut_controller;

  GtkSelectionModel *pages;
};

G_DEFINE_FINAL_TYPE (AdwTabView, adw_tab_view, GTK_TYPE_WIDGET)

enum {
  PROP_0,
  PROP_N_PAGES,
  PROP_N_PINNED_PAGES,
  PROP_IS_TRANSFERRING_PAGE,
  PROP_SELECTED_PAGE,
  PROP_DEFAULT_ICON,
  PROP_MENU_MODEL,
  PROP_SHORTCUT_WIDGET,
  PROP_PAGES,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

enum {
  SIGNAL_PAGE_ATTACHED,
  SIGNAL_PAGE_DETACHED,
  SIGNAL_PAGE_REORDERED,
  SIGNAL_CLOSE_PAGE,
  SIGNAL_SETUP_MENU,
  SIGNAL_CREATE_WINDOW,
  SIGNAL_INDICATOR_ACTIVATED,
  SIGNAL_LAST_SIGNAL,
};

static guint signals[SIGNAL_LAST_SIGNAL];

static void
set_page_selected (AdwTabPage *self,
                   gboolean    selected)
{
  g_return_if_fail (ADW_IS_TAB_PAGE (self));

  selected = !!selected;

  if (self->selected == selected)
    return;

  self->selected = selected;

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_SELECTED]);
}

static void
set_page_pinned (AdwTabPage *self,
                 gboolean    pinned)
{
  g_return_if_fail (ADW_IS_TAB_PAGE (self));

  pinned = !!pinned;

  if (self->pinned == pinned)
    return;

  self->pinned = pinned;

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_PINNED]);
}

static void set_page_parent (AdwTabPage *self,
                             AdwTabPage *parent);

static void
page_parent_notify_cb (AdwTabPage *self)
{
  AdwTabPage *grandparent = adw_tab_page_get_parent (self->parent);

  self->parent = NULL;

  if (grandparent)
    set_page_parent (self, grandparent);
  else
    g_object_notify_by_pspec (G_OBJECT (self), props[PAGE_PROP_PARENT]);
}

static void
set_page_parent (AdwTabPage *self,
                 AdwTabPage *parent)
{
  g_return_if_fail (ADW_IS_TAB_PAGE (self));
  g_return_if_fail (parent == NULL || ADW_IS_TAB_PAGE (parent));

  if (self->parent == parent)
    return;

  if (self->parent)
    g_object_weak_unref (G_OBJECT (self->parent),
                         (GWeakNotify) page_parent_notify_cb,
                         self);

  self->parent = parent;

  if (self->parent)
    g_object_weak_ref (G_OBJECT (self->parent),
                       (GWeakNotify) page_parent_notify_cb,
                       self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PAGE_PROP_PARENT]);
}

static void
adw_tab_page_dispose (GObject *object)
{
  AdwTabPage *self = ADW_TAB_PAGE (object);

  set_page_parent (self, NULL);

  G_OBJECT_CLASS (adw_tab_page_parent_class)->dispose (object);
}

static void
adw_tab_page_finalize (GObject *object)
{
  AdwTabPage *self = (AdwTabPage *)object;

  g_clear_object (&self->child);
  g_clear_pointer (&self->title, g_free);
  g_clear_pointer (&self->tooltip, g_free);
  g_clear_object (&self->icon);
  g_clear_object (&self->indicator_icon);

  G_OBJECT_CLASS (adw_tab_page_parent_class)->finalize (object);
}

static void
adw_tab_page_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  AdwTabPage *self = ADW_TAB_PAGE (object);

  switch (prop_id) {
  case PAGE_PROP_CHILD:
    g_value_set_object (value, adw_tab_page_get_child (self));
    break;

  case PAGE_PROP_PARENT:
    g_value_set_object (value, adw_tab_page_get_parent (self));
    break;

  case PAGE_PROP_SELECTED:
    g_value_set_boolean (value, adw_tab_page_get_selected (self));
    break;

  case PAGE_PROP_PINNED:
    g_value_set_boolean (value, adw_tab_page_get_pinned (self));
    break;

  case PAGE_PROP_TITLE:
    g_value_set_string (value, adw_tab_page_get_title (self));
    break;

  case PAGE_PROP_TOOLTIP:
    g_value_set_string (value, adw_tab_page_get_tooltip (self));
    break;

  case PAGE_PROP_ICON:
    g_value_set_object (value, adw_tab_page_get_icon (self));
    break;

  case PAGE_PROP_LOADING:
    g_value_set_boolean (value, adw_tab_page_get_loading (self));
    break;

  case PAGE_PROP_INDICATOR_ICON:
    g_value_set_object (value, adw_tab_page_get_indicator_icon (self));
    break;

  case PAGE_PROP_INDICATOR_ACTIVATABLE:
    g_value_set_boolean (value, adw_tab_page_get_indicator_activatable (self));
    break;

  case PAGE_PROP_NEEDS_ATTENTION:
    g_value_set_boolean (value, adw_tab_page_get_needs_attention (self));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
adw_tab_page_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  AdwTabPage *self = ADW_TAB_PAGE (object);

  switch (prop_id) {
  case PAGE_PROP_CHILD:
    g_set_object (&self->child, g_value_get_object (value));
    break;

  case PAGE_PROP_PARENT:
    set_page_parent (self, g_value_get_object (value));
    break;

  case PAGE_PROP_TITLE:
    adw_tab_page_set_title (self, g_value_get_string (value));
    break;

  case PAGE_PROP_TOOLTIP:
    adw_tab_page_set_tooltip (self, g_value_get_string (value));
    break;

  case PAGE_PROP_ICON:
    adw_tab_page_set_icon (self, g_value_get_object (value));
    break;

  case PAGE_PROP_LOADING:
    adw_tab_page_set_loading (self, g_value_get_boolean (value));
    break;

  case PAGE_PROP_INDICATOR_ICON:
    adw_tab_page_set_indicator_icon (self, g_value_get_object (value));
    break;

  case PAGE_PROP_INDICATOR_ACTIVATABLE:
    adw_tab_page_set_indicator_activatable (self, g_value_get_boolean (value));
    break;

  case PAGE_PROP_NEEDS_ATTENTION:
    adw_tab_page_set_needs_attention (self, g_value_get_boolean (value));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
adw_tab_page_class_init (AdwTabPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = adw_tab_page_dispose;
  object_class->finalize = adw_tab_page_finalize;
  object_class->get_property = adw_tab_page_get_property;
  object_class->set_property = adw_tab_page_set_property;

  /**
   * AdwTabPage:child: (attributes org.gtk.Property.get=adw_tab_page_get_child)
   *
   * The child of the page.
   *
   * Since: 1.0
   */
  page_props[PAGE_PROP_CHILD] =
    g_param_spec_object ("child",
                         "Child",
                         "The child of the page",
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  /**
   * AdwTabPage:parent: (attributes org.gtk.Property.get=adw_tab_page_get_parent)
   *
   * The parent page of the page.
   *
   * See [method@Adw.TabView.add_page] and [method@Adw.TabView.close_page].

   * Since: 1.0
   */
  page_props[PAGE_PROP_PARENT] =
    g_param_spec_object ("parent",
                         "Parent",
                         "The parent page of the page",
                         ADW_TYPE_TAB_PAGE,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabPage:selected: (attributes org.gtk.Property.get=adw_tab_page_get_selected)
   *
   * Whether the page is selected.
   *
   * Since: 1.0
   */
  page_props[PAGE_PROP_SELECTED] =
    g_param_spec_boolean ("selected",
                          "Selected",
                          "Whether the page is selected",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabPage:pinned: (attributes org.gtk.Property.get=adw_tab_page_get_pinned)
   *
   * Whether the page is pinned.
   *
   * See [method@Adw.TabView.set_page_pinned].
   *
   * Since: 1.0
   */
  page_props[PAGE_PROP_PINNED] =
    g_param_spec_boolean ("pinned",
                          "Pinned",
                          "Whether the page is pinned",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabPage:title: (attributes org.gtk.Property.get=adw_tab_page_get_title org.gtk.Property.set=adw_tab_page_set_title)
   *
   * The title of the page.
   *
   * [class@Adw.TabBar] will display it in the center of the tab unless it's
   * pinned, and will use it as a tooltip unless [property@Adw.TabPage:tooltip]
   * is set.
   *
   * Since: 1.0
   */
  page_props[PAGE_PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title of the page",
                         "",
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabPage:tooltip: (attributes org.gtk.Property.get=adw_tab_page_get_tooltip org.gtk.Property.set=adw_tab_page_set_tooltip)
   *
   * The tooltip of the page.
   *
   * The tooltip can be marked up with the Pango text markup language.
   *
   * If not set, [class@Adw.TabBar] will use [property@Adw.TabPage:title] as a
   * tooltip instead.
   *
   * Since: 1.0
   */
  page_props[PAGE_PROP_TOOLTIP] =
    g_param_spec_string ("tooltip",
                         "Tooltip",
                         "The tooltip of the page",
                         "",
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabPage:icon: (attributes org.gtk.Property.get=adw_tab_page_get_icon org.gtk.Property.set=adw_tab_page_set_icon)
   *
   * The icon of the page.
   *
   * [class@Adw.TabBar] displays the icon next to the title.
   *
   * It will not show the icon if [property@Adw.TabPage:loading] is set to
   * `TRUE`, or if the page is pinned and [property@Adw.TabPage:indicator-icon]
   * is set.
   *
   * Since: 1.0
   */
  page_props[PAGE_PROP_ICON] =
    g_param_spec_object ("icon",
                         "Icon",
                         "The icon of the page",
                         G_TYPE_ICON,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabPage:loading: (attributes org.gtk.Property.get=adw_tab_page_get_loading org.gtk.Property.set=adw_tab_page_set_loading)
   *
   * Whether the page is loading.
   *
   * If set to `TRUE`, [class@Adw.TabBar] will display a spinner in place of
   * icon.
   *
   * If the page is pinned and [property@Adw.TabPage:indicator-icon] is set, the
   * loading status will not be visible.
   *
   * Since: 1.0
   */
  page_props[PAGE_PROP_LOADING] =
    g_param_spec_boolean ("loading",
                          "Loading",
                          "Whether the page is loading",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabPage:indicator-icon: (attributes org.gtk.Property.get=adw_tab_page_get_indicator_icon org.gtk.Property.set=adw_tab_page_set_indicator_icon)
   *
   * An indicator icon for the page.
   *
   * A common use case is an audio or camera indicator in a web browser.
   *
   * [class@Adw.TabBar] will show it at the beginning of the tab, alongside icon
   * representing [property@Adw.TabPage:icon] or loading spinner.
   *
   * If the page is pinned, the indicator will be shown instead of icon or
   * spinner.
   *
   * If [property@Adw.TabPage:indicator-activatable] is set to `TRUE`, the
   * indicator icon can act as a button.
   *
   * Since: 1.0
   */
  page_props[PAGE_PROP_INDICATOR_ICON] =
    g_param_spec_object ("indicator-icon",
                         "Indicator icon",
                         "An indicator icon for the page",
                         G_TYPE_ICON,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabPage:indicator-activatable: (attributes org.gtk.Property.get=adw_tab_page_get_indicator_activatable org.gtk.Property.set=adw_tab_page_set_indicator_activatable)
   *
   * Whether the indicator icon is activatable.
   *
   * If set to `TRUE`, [signal@Adw.TabView::indicator-activated] will be emitted
   * when the indicator icon is clicked.
   *
   * If [property@Adw.TabPage:indicator-icon] is not set, does nothing.
   *
   * Since: 1.0
   */
  page_props[PAGE_PROP_INDICATOR_ACTIVATABLE] =
    g_param_spec_boolean ("indicator-activatable",
                          "Indicator activatable",
                          "Whether the indicator icon is activatable",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabPage:needs-attention: (attributes org.gtk.Property.get=adw_tab_page_get_needs_attention org.gtk.Property.set=adw_tab_page_set_needs_attention)
   *
   * Whether the page needs attention.
   *
   * [class@Adw.TabBar] will display a glow under the tab representing the page
   * if set to `TRUE`. If the tab is not visible, the corresponding edge of the
   * tab bar will be highlighted.
   *
   * Since: 1.0
   */
  page_props[PAGE_PROP_NEEDS_ATTENTION] =
    g_param_spec_boolean ("needs-attention",
                          "Needs attention",
                          "Whether the page needs attention",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PAGE_PROP, page_props);
}

static void
adw_tab_page_init (AdwTabPage *self)
{
  self->title = g_strdup ("");
  self->tooltip = g_strdup ("");
}

#define ADW_TYPE_TAB_PAGES (adw_tab_pages_get_type ())

G_DECLARE_FINAL_TYPE (AdwTabPages, adw_tab_pages, ADW, TAB_PAGES, GObject)

struct _AdwTabPages
{
  GObject parent_instance;

  AdwTabView *view;
};

static GType
adw_tab_pages_get_item_type (GListModel *model)
{
  return ADW_TYPE_TAB_PAGE;
}

static guint
adw_tab_pages_get_n_items (GListModel *model)
{
  AdwTabPages *self = ADW_TAB_PAGES (model);

  return self->view->n_pages;
}

static gpointer
adw_tab_pages_get_item (GListModel *model,
                        guint       position)
{
  AdwTabPages *self = ADW_TAB_PAGES (model);
  AdwTabPage *page = adw_tab_view_get_nth_page (self->view, position);

  if (!page)
    return NULL;

  return g_object_ref (page);
}

static void
adw_tab_pages_list_model_init (GListModelInterface *iface)
{
  iface->get_item_type = adw_tab_pages_get_item_type;
  iface->get_n_items = adw_tab_pages_get_n_items;
  iface->get_item = adw_tab_pages_get_item;
}

static gboolean
adw_tab_pages_is_selected (GtkSelectionModel *model,
                           guint              position)
{
  AdwTabPages *self = ADW_TAB_PAGES (model);
  AdwTabPage *page;

  page = adw_tab_view_get_nth_page (self->view, position);

  return page->selected;
}

static gboolean
adw_tab_pages_select_item (GtkSelectionModel *model,
                           guint              position,
                           gboolean           exclusive)
{
  AdwTabPages *self = ADW_TAB_PAGES (model);
  AdwTabPage *page;

  page = adw_tab_view_get_nth_page (self->view, position);

  adw_tab_view_set_selected_page (self->view, page);

  return TRUE;
}

static void
adw_tab_pages_selection_model_init (GtkSelectionModelInterface *iface)
{
  iface->is_selected = adw_tab_pages_is_selected;
  iface->select_item = adw_tab_pages_select_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (AdwTabPages, adw_tab_pages, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, adw_tab_pages_list_model_init)
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_SELECTION_MODEL, adw_tab_pages_selection_model_init))

static void
adw_tab_pages_init (AdwTabPages *self)
{
}

static void
adw_tab_pages_class_init (AdwTabPagesClass *klass)
{
}

static GtkSelectionModel *
adw_tab_pages_new (AdwTabView *view)
{
  AdwTabPages *pages;

  pages = g_object_new (ADW_TYPE_TAB_PAGES, NULL);
  pages->view = view;

  return GTK_SELECTION_MODEL (pages);
}

static gboolean
object_handled_accumulator (GSignalInvocationHint *ihint,
                            GValue                *return_accu,
                            const GValue          *handler_return,
                            gpointer               data)
{
  GObject *object = g_value_get_object (handler_return);

  g_value_set_object (return_accu, object);

  return !object;
}

static void
begin_transfer_for_group (AdwTabView *self)
{
  GSList *l;

  for (l = tab_view_list; l; l = l->next) {
    AdwTabView *view = l->data;

    view->transfer_count++;

    if (view->transfer_count == 1)
      g_object_notify_by_pspec (G_OBJECT (view), props[PROP_IS_TRANSFERRING_PAGE]);
  }
}

static void
end_transfer_for_group (AdwTabView *self)
{
  GSList *l;

  for (l = tab_view_list; l; l = l->next) {
    AdwTabView *view = l->data;

    view->transfer_count--;

    if (view->transfer_count == 0)
      g_object_notify_by_pspec (G_OBJECT (view), props[PROP_IS_TRANSFERRING_PAGE]);
  }
}

static void
set_n_pages (AdwTabView *self,
             int         n_pages)
{
  if (n_pages == self->n_pages)
    return;

  self->n_pages = n_pages;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_PAGES]);
}

static void
set_n_pinned_pages (AdwTabView *self,
                    int         n_pinned_pages)
{
  if (n_pinned_pages == self->n_pinned_pages)
    return;

  self->n_pinned_pages = n_pinned_pages;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_PINNED_PAGES]);
}

static inline gboolean
page_belongs_to_this_view (AdwTabView *self,
                           AdwTabPage *page)
{
  if (!page)
    return FALSE;

  return gtk_widget_get_parent (page->child) == GTK_WIDGET (self->stack);
}

static inline gboolean
is_descendant_of (AdwTabPage *page,
                  AdwTabPage *parent)
{
  while (page && page != parent)
    page = adw_tab_page_get_parent (page);

  return page == parent;
}

static void
attach_page (AdwTabView *self,
             AdwTabPage *page,
             int         position)
{
  GtkWidget *child = adw_tab_page_get_child (page);
  AdwTabPage *parent;

  g_list_store_insert (self->children, position, page);

  gtk_stack_add_child (self->stack, child);

  g_object_freeze_notify (G_OBJECT (self));

  set_n_pages (self, self->n_pages + 1);

  if (adw_tab_page_get_pinned (page))
    set_n_pinned_pages (self, self->n_pinned_pages + 1);

  g_object_thaw_notify (G_OBJECT (self));

  parent = adw_tab_page_get_parent (page);

  if (parent && !page_belongs_to_this_view (self, parent))
    set_page_parent (page, NULL);

  g_signal_emit (self, signals[SIGNAL_PAGE_ATTACHED], 0, page, position);
}

static void
set_selected_page (AdwTabView *self,
                   AdwTabPage *selected_page,
                   gboolean    notify_pages)
{
  guint old_position = GTK_INVALID_LIST_POSITION;
  guint new_position = GTK_INVALID_LIST_POSITION;

  if (self->selected_page == selected_page)
    return;

  if (self->selected_page) {
    if (notify_pages && self->pages)
      old_position = adw_tab_view_get_page_position (self, self->selected_page);

    set_page_selected (self->selected_page, FALSE);
  }

  self->selected_page = selected_page;

  if (self->selected_page) {
    if (notify_pages && self->pages)
      new_position = adw_tab_view_get_page_position (self, self->selected_page);

    gtk_stack_set_visible_child (self->stack,
                                 adw_tab_page_get_child (selected_page));
    set_page_selected (self->selected_page, TRUE);
  }

  if (notify_pages && self->pages) {
    if (old_position == GTK_INVALID_LIST_POSITION && new_position == GTK_INVALID_LIST_POSITION)
      ; /* nothing to do */
    else if (old_position == GTK_INVALID_LIST_POSITION)
      gtk_selection_model_selection_changed (self->pages, new_position, 1);
    else if (new_position == GTK_INVALID_LIST_POSITION)
      gtk_selection_model_selection_changed (self->pages, old_position, 1);
    else
      gtk_selection_model_selection_changed (self->pages,
                                             MIN (old_position, new_position),
                                             MAX (old_position, new_position) -
                                             MIN (old_position, new_position) + 1);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED_PAGE]);
}

static void
select_previous_page (AdwTabView *self,
                      AdwTabPage *page)
{
  int pos = adw_tab_view_get_page_position (self, page);
  AdwTabPage *parent;

  if (page != self->selected_page)
    return;

  parent = adw_tab_page_get_parent (page);

  if (parent && pos > 0) {
    AdwTabPage *prev_page = adw_tab_view_get_nth_page (self, pos - 1);

    /* This usually means we opened a few pages from the same page in a row, or
     * the previous page is the parent. Switch there. */
    if (is_descendant_of (prev_page, parent)) {
      adw_tab_view_set_selected_page (self, prev_page);

      return;
    }

    /* Pinned pages are special in that opening a page from a pinned parent
     * will place it not directly after the parent, but after the last pinned
     * page. This means that if we're closing the first non-pinned page, we need
     * to jump to the parent directly instead of the previous page which might
     * be different. */
    if (adw_tab_page_get_pinned (prev_page) &&
        adw_tab_page_get_pinned (parent)) {
      adw_tab_view_set_selected_page (self, parent);

      return;
    }
  }

  if (adw_tab_view_select_next_page (self))
    return;

  adw_tab_view_select_previous_page (self);
}

static void
detach_page (AdwTabView *self,
             AdwTabPage *page,
             gboolean    notify_pages)
{
  int pos = adw_tab_view_get_page_position (self, page);
  GtkWidget *child;

  select_previous_page (self, page);

  child = adw_tab_page_get_child (page);

  g_object_ref (page);
  g_object_ref (child);

  if (self->n_pages == 1)
    set_selected_page (self, NULL, notify_pages);

  g_list_store_remove (self->children, pos);

  g_object_freeze_notify (G_OBJECT (self));

  set_n_pages (self, self->n_pages - 1);

  if (adw_tab_page_get_pinned (page))
    set_n_pinned_pages (self, self->n_pinned_pages - 1);

  g_object_thaw_notify (G_OBJECT (self));

  gtk_stack_remove (self->stack, child);

  g_signal_emit (self, signals[SIGNAL_PAGE_DETACHED], 0, page, pos);

  if (notify_pages && self->pages)
    g_list_model_items_changed (G_LIST_MODEL (self->pages), pos, 1, 0);

  g_object_unref (child);
  g_object_unref (page);
}

static AdwTabPage *
insert_page (AdwTabView *self,
             GtkWidget  *child,
             AdwTabPage *parent,
             int         position,
             gboolean    pinned)
{
  g_autoptr (AdwTabPage) page =
    g_object_new (ADW_TYPE_TAB_PAGE,
                  "child", child,
                  "parent", parent,
                  NULL);

  set_page_pinned (page, pinned);

  attach_page (self, page, position);

  g_object_freeze_notify (G_OBJECT (self));

  if (!self->selected_page)
    set_selected_page (self, page, FALSE);

  if (self->pages)
    g_list_model_items_changed (G_LIST_MODEL (self->pages), position, 0, 1);

  g_object_thaw_notify (G_OBJECT (self));

  return page;
}

static gboolean
close_page_cb (AdwTabView *self,
               AdwTabPage *page)
{
  adw_tab_view_close_page_finish (self, page,
                                  !adw_tab_page_get_pinned (page));

  return GDK_EVENT_STOP;
}

static gboolean
select_page (AdwTabView       *self,
             GtkDirectionType  direction,
             gboolean          last)
{
  gboolean is_rtl, success = last;

  if (!self->selected_page)
    return FALSE;

  is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  if (direction == GTK_DIR_LEFT)
    direction = is_rtl ? GTK_DIR_TAB_FORWARD : GTK_DIR_TAB_BACKWARD;
  else if (direction == GTK_DIR_RIGHT)
    direction = is_rtl ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;

  if (direction == GTK_DIR_TAB_BACKWARD) {
    if (last)
      success = adw_tab_view_select_first_page (self);
    else
      success = adw_tab_view_select_previous_page (self);
  } else if (direction == GTK_DIR_TAB_FORWARD) {
    if (last)
      success = adw_tab_view_select_last_page (self);
    else
      success = adw_tab_view_select_next_page (self);
  }

  gtk_widget_grab_focus (adw_tab_page_get_child (self->selected_page));

  return success;
}

static gboolean
reorder_page (AdwTabView       *self,
              GtkDirectionType  direction,
              gboolean          last)
{
  gboolean is_rtl, success = last;

  if (!self->selected_page)
    return FALSE;

  is_rtl = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  if (direction == GTK_DIR_LEFT)
    direction = is_rtl ? GTK_DIR_TAB_FORWARD : GTK_DIR_TAB_BACKWARD;
  else if (direction == GTK_DIR_RIGHT)
    direction = is_rtl ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD;

  if (direction == GTK_DIR_TAB_BACKWARD) {
    if (last)
      success = adw_tab_view_reorder_first (self, self->selected_page);
    else
      success = adw_tab_view_reorder_backward (self, self->selected_page);
  } else if (direction == GTK_DIR_TAB_FORWARD) {
    if (last)
      success = adw_tab_view_reorder_last (self, self->selected_page);
    else
      success = adw_tab_view_reorder_forward (self, self->selected_page);
  }

  return success;
}

static inline gboolean
handle_select_reorder_shortcuts (AdwTabView       *self,
                                 guint             keyval,
                                 GdkModifierType   state,
                                 guint             keysym,
                                 GtkDirectionType  direction,
                                 gboolean          last)
{
  /* All keypad keysyms are aligned at the same order as non-keypad ones */
  guint keypad_keysym = keysym - GDK_KEY_Left + GDK_KEY_KP_Left;
  gboolean success = FALSE;

  if (keyval != keysym && keyval != keypad_keysym)
    return GDK_EVENT_PROPAGATE;

  if (state == GDK_CONTROL_MASK)
    success = select_page (self, direction, last);
  else if (state == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
    success = reorder_page (self, direction, last);
  else
    return GDK_EVENT_PROPAGATE;

  if (!success)
    gtk_widget_error_bell (GTK_WIDGET (self));

  return GDK_EVENT_STOP;
}

static gboolean
select_page_cb (GtkWidget  *widget,
                GVariant   *args,
                AdwTabView *self)
{
  GtkDirectionType direction;
  gboolean last, loop, success = FALSE;

  if (!adw_tab_view_get_selected_page (self))
    return GDK_EVENT_PROPAGATE;

  g_variant_get (args, "(hbb)", &direction, &last, &loop);

  if (direction == GTK_DIR_TAB_BACKWARD) {
    if (last)
      success = adw_tab_view_select_first_page (self);
    else
      success = adw_tab_view_select_previous_page (self);

    if (!success && loop) {
      AdwTabPage *page = adw_tab_view_get_nth_page (self, self->n_pages - 1);

      adw_tab_view_set_selected_page (self, page);

      success = TRUE;
    }
  } else if (direction == GTK_DIR_TAB_FORWARD) {
    if (last)
      success = adw_tab_view_select_last_page (self);
    else
      success = adw_tab_view_select_next_page (self);

    if (!success && loop) {
      AdwTabPage *page = adw_tab_view_get_nth_page (self, 0);

      adw_tab_view_set_selected_page (self, page);

      success = TRUE;
    }
  }

  if (!success)
    gtk_widget_error_bell (GTK_WIDGET (self));

  return success;
}

static inline void
add_switch_shortcut (AdwTabView         *self,
                     GtkEventController *controller,
                     guint               keysym,
                     guint               keypad_keysym,
                     GdkModifierType     modifiers,
                     GtkDirectionType    direction,
                     gboolean            last,
                     gboolean            loop)
{
  GtkShortcutTrigger *trigger;
  GtkShortcutAction *action;
  GtkShortcut *shortcut;

  trigger = gtk_alternative_trigger_new (gtk_keyval_trigger_new (keysym, modifiers),
                                         gtk_keyval_trigger_new (keypad_keysym, modifiers));
  action = gtk_callback_action_new ((GtkShortcutFunc) select_page_cb, self, NULL);
  shortcut = gtk_shortcut_new (trigger, action);

  gtk_shortcut_set_arguments (shortcut, g_variant_new ("(hbb)", direction, last, loop));
  gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller),
                                        shortcut);
}

static gboolean
reorder_page_cb (GtkWidget  *widget,
                 GVariant   *args,
                 AdwTabView *self)
{
  GtkDirectionType direction;
  gboolean last, success = FALSE;
  AdwTabPage *page = adw_tab_view_get_selected_page (self);

  if (!page)
    return GDK_EVENT_PROPAGATE;

  g_variant_get (args, "(hb)", &direction, &last);

  if (direction == GTK_DIR_TAB_BACKWARD) {
    if (last)
      success = adw_tab_view_reorder_first (self, page);
    else
      success = adw_tab_view_reorder_backward (self, page);
  } else if (direction == GTK_DIR_TAB_FORWARD) {
    if (last)
      success = adw_tab_view_reorder_last (self, page);
    else
      success = adw_tab_view_reorder_forward (self, page);
  }

  if (!success)
    gtk_widget_error_bell (GTK_WIDGET (self));

  return success;
}

static inline void
add_reorder_shortcut (AdwTabView         *self,
                      GtkEventController *controller,
                      guint               keysym,
                      guint               keypad_keysym,
                      GtkDirectionType    direction,
                      gboolean            last)
{
  GtkShortcutTrigger *trigger;
  GtkShortcutAction *action;
  GtkShortcut *shortcut;

  trigger = gtk_alternative_trigger_new (gtk_keyval_trigger_new (keysym, GDK_CONTROL_MASK | GDK_SHIFT_MASK),
                                         gtk_keyval_trigger_new (keypad_keysym, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
  action = gtk_callback_action_new ((GtkShortcutFunc) reorder_page_cb, self, NULL);
  shortcut = gtk_shortcut_new (trigger, action);

  gtk_shortcut_set_arguments (shortcut, g_variant_new ("(hb)", direction, last));
  gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller),
                                        shortcut);
}

static gboolean
select_nth_page_cb (GtkWidget  *widget,
                    GVariant   *args,
                    AdwTabView *self)
{
  gint8 n_page = g_variant_get_byte (args);
  AdwTabPage *page;

  if (n_page >= self->n_pages)
    return GDK_EVENT_PROPAGATE;

  page = adw_tab_view_get_nth_page (self, n_page);
  adw_tab_view_set_selected_page (self, page);

  return GDK_EVENT_STOP;
}

static inline void
add_switch_nth_page_shortcut (AdwTabView         *self,
                              GtkEventController *controller,
                              guint               keysym,
                              guint               keypad_keysym,
                              int                 n_page)
{
  GtkShortcutTrigger *trigger;
  GtkShortcutAction *action;
  GtkShortcut *shortcut;

  trigger = gtk_alternative_trigger_new (gtk_keyval_trigger_new (keysym, GDK_ALT_MASK),
                                         gtk_keyval_trigger_new (keypad_keysym, GDK_ALT_MASK));
  action = gtk_callback_action_new ((GtkShortcutFunc) select_nth_page_cb, self, NULL);
  shortcut = gtk_shortcut_new (trigger, action);

  gtk_shortcut_set_arguments (shortcut, g_variant_new_byte (n_page));
  gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller),
                                        shortcut);
}

static void
init_shortcuts (AdwTabView         *self,
                GtkEventController *controller)
{
  int i;

  add_switch_shortcut (self, controller,
                       GDK_KEY_Tab, GDK_KEY_KP_Tab, GDK_CONTROL_MASK,
                       GTK_DIR_TAB_FORWARD, FALSE, TRUE);
  add_switch_shortcut (self, controller,
                       GDK_KEY_Tab, GDK_KEY_KP_Tab, GDK_CONTROL_MASK | GDK_SHIFT_MASK,
                       GTK_DIR_TAB_BACKWARD, FALSE, TRUE);
  add_switch_shortcut (self, controller,
                       GDK_KEY_Page_Up, GDK_KEY_KP_Page_Up, GDK_CONTROL_MASK,
                       GTK_DIR_TAB_BACKWARD, FALSE, FALSE);
  add_switch_shortcut (self, controller,
                       GDK_KEY_Page_Down, GDK_KEY_KP_Page_Down, GDK_CONTROL_MASK,
                       GTK_DIR_TAB_FORWARD, FALSE, FALSE);
  add_switch_shortcut (self, controller,
                       GDK_KEY_Home, GDK_KEY_KP_Home, GDK_CONTROL_MASK,
                       GTK_DIR_TAB_BACKWARD, TRUE, FALSE);
  add_switch_shortcut (self, controller,
                       GDK_KEY_End, GDK_KEY_KP_End, GDK_CONTROL_MASK,
                       GTK_DIR_TAB_FORWARD, TRUE, FALSE);

  add_reorder_shortcut (self, controller,
                        GDK_KEY_Page_Up, GDK_KEY_KP_Page_Up,
                        GTK_DIR_TAB_BACKWARD, FALSE);
  add_reorder_shortcut (self, controller,
                        GDK_KEY_Page_Down, GDK_KEY_KP_Page_Down,
                        GTK_DIR_TAB_FORWARD, FALSE);
  add_reorder_shortcut (self, controller,
                        GDK_KEY_Home, GDK_KEY_KP_Home,
                        GTK_DIR_TAB_BACKWARD, TRUE);
  add_reorder_shortcut (self, controller,
                        GDK_KEY_End, GDK_KEY_KP_End,
                        GTK_DIR_TAB_FORWARD,  TRUE);

  for (i = 0; i < 10; i++)
    add_switch_nth_page_shortcut (self,
                                  controller,
                                  GDK_KEY_0 + i,
                                  GDK_KEY_KP_0 + i,
                                  (i + 9) % 10); /* Alt+0 means page 10, not 0 */
}

static void
shortcut_widget_notify_cb (AdwTabView *self)
{
  gtk_widget_remove_controller (self->shortcut_widget, self->shortcut_controller);
  self->shortcut_controller = NULL;
  self->shortcut_widget = NULL;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHORTCUT_WIDGET]);
}

static void
adw_tab_view_dispose (GObject *object)
{
  AdwTabView *self = ADW_TAB_VIEW (object);

  adw_tab_view_set_shortcut_widget (self, NULL);

  if (self->pages)
    g_list_model_items_changed (G_LIST_MODEL (self->pages), 0, self->n_pages, 0);

  while (self->n_pages) {
    AdwTabPage *page = adw_tab_view_get_nth_page (self, 0);

    detach_page (self, page, FALSE);
  }

  g_clear_object (&self->children);

  g_clear_pointer ((GtkWidget **) &self->stack, gtk_widget_unparent);

  G_OBJECT_CLASS (adw_tab_view_parent_class)->dispose (object);
}

static void
adw_tab_view_finalize (GObject *object)
{
  AdwTabView *self = (AdwTabView *) object;

  if (self->pages)
    g_object_remove_weak_pointer (G_OBJECT (self->pages),
                                  (gpointer *) &self->pages);

  g_clear_object (&self->default_icon);
  g_clear_object (&self->menu_model);

  tab_view_list = g_slist_remove (tab_view_list, self);

  G_OBJECT_CLASS (adw_tab_view_parent_class)->finalize (object);
}

static void
adw_tab_view_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  AdwTabView *self = ADW_TAB_VIEW (object);

  switch (prop_id) {
  case PROP_N_PAGES:
    g_value_set_int (value, adw_tab_view_get_n_pages (self));
    break;

  case PROP_N_PINNED_PAGES:
    g_value_set_int (value, adw_tab_view_get_n_pinned_pages (self));
    break;

  case PROP_IS_TRANSFERRING_PAGE:
    g_value_set_boolean (value, adw_tab_view_get_is_transferring_page (self));
    break;

  case PROP_SELECTED_PAGE:
    g_value_set_object (value, adw_tab_view_get_selected_page (self));
    break;

  case PROP_DEFAULT_ICON:
    g_value_set_object (value, adw_tab_view_get_default_icon (self));
    break;

  case PROP_MENU_MODEL:
    g_value_set_object (value, adw_tab_view_get_menu_model (self));
    break;

  case PROP_SHORTCUT_WIDGET:
    g_value_set_object (value, adw_tab_view_get_shortcut_widget (self));
    break;

  case PROP_PAGES:
    g_value_take_object (value, adw_tab_view_get_pages (self));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
adw_tab_view_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  AdwTabView *self = ADW_TAB_VIEW (object);

  switch (prop_id) {
  case PROP_SELECTED_PAGE:
    adw_tab_view_set_selected_page (self, g_value_get_object (value));
    break;

  case PROP_DEFAULT_ICON:
    adw_tab_view_set_default_icon (self, g_value_get_object (value));
    break;

  case PROP_MENU_MODEL:
    adw_tab_view_set_menu_model (self, g_value_get_object (value));
    break;

  case PROP_SHORTCUT_WIDGET:
    adw_tab_view_set_shortcut_widget (self, g_value_get_object (value));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
adw_tab_view_class_init (AdwTabViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = adw_tab_view_dispose;
  object_class->finalize = adw_tab_view_finalize;
  object_class->get_property = adw_tab_view_get_property;
  object_class->set_property = adw_tab_view_set_property;

  widget_class->compute_expand = adw_widget_compute_expand;

  /**
   * AdwTabView:n-pages: (attributes org.gtk.Property.get=adw_tab_view_get_n_pages)
   *
   * The number of pages in the tab view.
   *
   * Since: 1.0
   */
  props[PROP_N_PAGES] =
    g_param_spec_int ("n-pages",
                      "Number of pages",
                      "The number of pages in the tab view",
                      0, G_MAXINT, 0,
                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabView:n-pinned-pages: (attributes org.gtk.Property.get=adw_tab_view_get_n_pinned_pages)
   *
   * The number of pinned pages in the tab view.
   *
   * See [method@Adw.TabView.set_page_pinned].
   *
   * Since: 1.0
   */
  props[PROP_N_PINNED_PAGES] =
    g_param_spec_int ("n-pinned-pages",
                      "Number of pinned pages",
                      "The number of pinned pages in the tab view",
                      0, G_MAXINT, 0,
                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabView:is-transferring-page: (attributes org.gtk.Property.get=adw_tab_view_get_is_transferring_page)
   *
   * Whether a page is being transferred.
   *
   * This property will be set to `TRUE` when a drag-n-drop tab transfer starts
   * on any `AdwTabView`, and to `FALSE` after it ends.
   *
   * During the transfer, children cannot receive pointer input and a tab can
   * be safely dropped on the tab view.
   *
   * Since: 1.0
   */
  props[PROP_IS_TRANSFERRING_PAGE] =
    g_param_spec_boolean ("is-transferring-page",
                          "Is transferring page",
                          "Whether a page is being transferred",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabView:selected-page: (attributes org.gtk.Property.get=adw_tab_view_get_selected_page org.gtk.Property.set=adw_tab_view_set_selected_page)
   *
   * The currently selected page.
   *
   * Since: 1.0
   */
  props[PROP_SELECTED_PAGE] =
    g_param_spec_object ("selected-page",
                         "Selected page",
                         "The currently selected page",
                         ADW_TYPE_TAB_PAGE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabView:default-icon: (attributes org.gtk.Property.get=adw_tab_view_get_default_icon org.gtk.Property.set=adw_tab_view_set_default_icon)
   *
   * Default page icon.
   *
   * If a page doesn't provide its own icon via [property@Adw.TabPage:icon],
   * a default icon may be used instead for contexts where having an icon is
   * necessary.
   *
   * [class@Adw.TabBar] will use default icon for pinned tabs in case the page
   * is not loading, doesn't have an icon and an indicator. Default icon is
   * never used for tabs that aren't pinned.
   *
   * By default, the `adw-tab-icon-missing-symbolic` icon is used.
   *
   * Since: 1.0
   */
  props[PROP_DEFAULT_ICON] =
    g_param_spec_object ("default-icon",
                         "Default icon",
                         "Default page icon",
                         G_TYPE_ICON,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabView:menu-model: (attributes org.gtk.Property.get=adw_tab_view_get_menu_model org.gtk.Property.set=adw_tab_view_set_menu_model)
   *
   * Tab context menu model.
   *
   * When a context menu is shown for a tab, it will be constructed from the
   * provided menu model. Use the [signal@Adw.TabView::setup-menu] signal to set
   * up the menu actions for the particular tab.
   *
   * Since: 1.0
   */
  props[PROP_MENU_MODEL] =
    g_param_spec_object ("menu-model",
                         "Menu model",
                         "Tab context menu model",
                         G_TYPE_MENU_MODEL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabView:shortcut-widget: (attributes org.gtk.Property.get=adw_tab_view_get_shortcut_widget org.gtk.Property.set=adw_tab_view_set_shortcut_widget)
   *
   * The shortcut widget.
   *
   * It has the following shortcuts:
   * * Ctrl+Page Up - switch to the previous page
   * * Ctrl+Page Down - switch to the next page
   * * Ctrl+Home - switch to the first page
   * * Ctrl+End - switch to the last page
   * * Ctrl+Shift+Page Up - move the current page backward
   * * Ctrl+Shift+Page Down - move the current page forward
   * * Ctrl+Shift+Home - move the current page at the start
   * * Ctrl+Shift+End - move the current page at the end
   * * Ctrl+Tab - switch to the next page, with looping
   * * Ctrl+Shift+Tab - switch to the previous page, with looping
   * * Alt+1-9 - switch to pages 1-9
   * * Alt+0 - switch to page 10
   *
   * These shortcuts are always available on the tab view itself, this property
   * is useful if they should be available globally.
   *
   * Since: 1.0
   */
  props[PROP_SHORTCUT_WIDGET] =
    g_param_spec_object ("shortcut-widget",
                         "Shortcut widget",
                         "Tab shortcut widget",
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * AdwTabView:pages: (attributes org.gtk.Property.get=adw_tab_view_get_pages)
   *
   * A selection model with the tab view's pages.
   *
   * This can be used to keep an up-to-date view. The model also implements
   * [iface@Gtk.SelectionModel] and can be used to track and change the selected
   * page.
   *
   * Since: 1.0
   */
  props[PROP_PAGES] =
    g_param_spec_object ("pages",
                         "Pages",
                         "A selection model with the tab view's pages",
                         GTK_TYPE_SELECTION_MODEL,
                         G_PARAM_READABLE);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  /**
   * AdwTabView::page-attached:
   * @self: a `AdwTabView`
   * @page: a page of @self
   * @position: the position of the page, starting from 0
   *
   * Emitted when a page has been created or transferred to @self.
   *
   * A typical reason to connect to this signal would be to connect to page
   * signals for things such as updating window title.
   *
   * Since: 1.0
   */
  signals[SIGNAL_PAGE_ATTACHED] =
    g_signal_new ("page-attached",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2,
                  ADW_TYPE_TAB_PAGE, G_TYPE_INT);

  /**
   * AdwTabView::page-detached:
   * @self: a `AdwTabView`
   * @page: a page of @self
   * @position: the position of the removed page, starting from 0
   *
   * Emitted when a page has been removed or transferred to another view.
   *
   * A typical reason to connect to this signal would be to disconnect signal
   * handlers connected in the [signal@Adw.TabView::page-attached] handler.
   *
   * It is important not to try and destroy the page child in the handler of
   * this function as the child might merely be moved to another window; use
   * child dispose handler for that or do it in sync with your
   * [method@Adw.TabView.close_page_finish] calls.
   *
   * Since: 1.0
   */
  signals[SIGNAL_PAGE_DETACHED] =
    g_signal_new ("page-detached",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2,
                  ADW_TYPE_TAB_PAGE, G_TYPE_INT);

  /**
   * AdwTabView::page-reordered:
   * @self: a `AdwTabView`
   * @page: a page of @self
   * @position: the position @page was moved to, starting at 0
   *
   * Emitted after @page has been reordered to @position.
   *
   * Since: 1.0
   */
  signals[SIGNAL_PAGE_REORDERED] =
    g_signal_new ("page-reordered",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2,
                  ADW_TYPE_TAB_PAGE, G_TYPE_INT);

  /**
   * AdwTabView::close-page:
   * @self: a `AdwTabView`
   * @page: a page of @self
   *
   * Emitted after [method@Adw.TabView.close_page] has been called for
   * @page.
   *
   * The handler is expected to call [method@Adw.TabView.close_page_finish] to
   * confirm or reject the closing.
   *
   * The default handler will immediately confirm closing for non-pinned pages,
   * or reject it for pinned pages, equivalent to the following example:
   *
   * ```c
   * static gboolean
   * close_page_cb (AdwTabView *view,
   *                AdwTabPage *page,
   *                gpointer    user_data)
   * {
   *   adw_tab_view_close_page_finish (view, page, !adw_tab_page_get_pinned (page));
   *
   *   return GDK_EVENT_STOP;
   * }
   * ```
   *
   * The [method@Adw.TabView.close_page_finish] call doesn't have to happen
   * inside the handler, so can be used to do asynchronous checks before
   * confirming the closing.
   *
   * A typical reason to connect to this signal is to show a confirmation dialog
   * for closing a tab.
   *
   * Since: 1.0
   */
  signals[SIGNAL_CLOSE_PAGE] =
    g_signal_new ("close-page",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  g_signal_accumulator_true_handled,
                  NULL, NULL,
                  G_TYPE_BOOLEAN,
                  1,
                  ADW_TYPE_TAB_PAGE);

  /**
   * AdwTabView::setup-menu:
   * @self: a `AdwTabView`
   * @page: (nullable): a page of @self
   *
   * Emitted when a context menu is opened or closed for @page.
   *
   * If the menu has been closed, @page will be set to `NULL`.
   *
   * It can be used to set up menu actions before showing the menu, for example
   * disable actions not applicable to @page.
   *
   * Since: 1.0
   */
  signals[SIGNAL_SETUP_MENU] =
    g_signal_new ("setup-menu",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  ADW_TYPE_TAB_PAGE);

  /**
   * AdwTabView::create-window:
   * @self: a `AdwTabView`
   *
   * Emitted when a tab should be transferred into a new window.
   *
   * This can happen after a tab has been dropped on desktop.
   *
   * The signal handler is expected to create a new window, position it as
   * needed and return its `AdwTabView` that the page will be transferred into.
   *
   * Returns: (transfer none) (nullable): the `AdwTabView` from the new window
   *
   * Since: 1.0
   */
  signals[SIGNAL_CREATE_WINDOW] =
    g_signal_new ("create-window",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  object_handled_accumulator,
                  NULL, NULL,
                  ADW_TYPE_TAB_VIEW,
                  0);

  /**
   * AdwTabView::indicator-activated:
   * @self: a `AdwTabView`
   * @page: a page of @self
   *
   * Emitted after the indicator icon on @page has been activated.
   *
   * See [property@Adw.TabPage:indicator-icon] and
   * [property@Adw.TabPage:indicator-activatable].
   *
   * Since: 1.0
   */
  signals[SIGNAL_INDICATOR_ACTIVATED] =
    g_signal_new ("indicator-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  ADW_TYPE_TAB_PAGE);

  g_signal_override_class_handler ("close-page",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_CALLBACK (close_page_cb));

  gtk_widget_class_set_css_name (widget_class, "tabview");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
adw_tab_view_init (AdwTabView *self)
{
  GtkEventController *controller;

  self->children = g_list_store_new (ADW_TYPE_TAB_PAGE);
  self->default_icon = G_ICON (g_themed_icon_new ("adw-tab-icon-missing-symbolic"));

  self->stack = GTK_STACK (gtk_stack_new ());
  gtk_widget_show (GTK_WIDGET (self->stack));
  gtk_widget_set_parent (GTK_WIDGET (self->stack), GTK_WIDGET (self));

  g_object_bind_property (self, "is-transferring-page",
                          self->stack, "can-target",
                          G_BINDING_INVERT_BOOLEAN);

  tab_view_list = g_slist_prepend (tab_view_list, self);

  controller = gtk_shortcut_controller_new ();

  init_shortcuts (self, controller);

  gtk_widget_add_controller (GTK_WIDGET (self), controller);
}

/**
 * adw_tab_page_get_child: (attributes org.gtk.Method.get_property=child)
 * @self: a `AdwTabPage`
 *
 * Gets the child of @self.
 *
 * Returns: (transfer none): the child of @self
 *
 * Since: 1.0
 */
GtkWidget *
adw_tab_page_get_child (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), NULL);

  return self->child;
}

/**
 * adw_tab_page_get_parent: (attributes org.gtk.Method.get_property=parent)
 * @self: a `AdwTabPage`
 *
 * Gets the parent page of @self.
 *
 * Returns: (transfer none) (nullable): the parent page
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_page_get_parent (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), NULL);

  return self->parent;
}

/**
 * adw_tab_page_get_selected: (attributes org.gtk.Method.get_property=selected)
 * @self: a `AdwTabPage`
 *
 * Gets whether @self is selected.
 *
 * Returns: whether @self is selected
 *
 * Since: 1.0
 */
gboolean
adw_tab_page_get_selected (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), FALSE);

  return self->selected;
}

/**
 * adw_tab_page_get_pinned: (attributes org.gtk.Method.get_property=pinned)
 * @self: a `AdwTabPage`
 *
 * Gets whether @self is pinned.
 *
 * Returns: whether @self is pinned
 *
 * Since: 1.0
 */
gboolean
adw_tab_page_get_pinned (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), FALSE);

  return self->pinned;
}

/**
 * adw_tab_page_get_title: (attributes org.gtk.Method.get_property=title)
 * @self: a `AdwTabPage`
 *
 * Gets the title of @self.
 *
 * Returns: the title of @self
 *
 * Since: 1.0
 */
const char *
adw_tab_page_get_title (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), NULL);

  return self->title;
}

/**
 * adw_tab_page_set_title: (attributes org.gtk.Method.set_property=title)
 * @self: a `AdwTabPage`
 * @title: the title of @self
 *
 * Sets the title of @self.
 *
 * Since: 1.0
 */
void
adw_tab_page_set_title (AdwTabPage *self,
                        const char *title)
{
  g_return_if_fail (ADW_IS_TAB_PAGE (self));

  if (!g_strcmp0 (title, self->title))
    return;

  g_clear_pointer (&self->title, g_free);
  self->title = g_strdup (title ? title : "");

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_TITLE]);
}

/**
 * adw_tab_page_get_tooltip: (attributes org.gtk.Method.get_property=tooltip)
 * @self: a `AdwTabPage`
 *
 * Gets the tooltip of @self.
 *
 * Returns: (nullable): the tooltip of @self
 *
 * Since: 1.0
 */
const char *
adw_tab_page_get_tooltip (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), NULL);

  return self->tooltip;
}

/**
 * adw_tab_page_set_tooltip: (attributes org.gtk.Method.set_property=tooltip)
 * @self: a `AdwTabPage`
 * @tooltip: the tooltip of @self
 *
 * Sets the tooltip of @self.
 *
 * Since: 1.0
 */
void
adw_tab_page_set_tooltip (AdwTabPage *self,
                          const char *tooltip)
{
  g_return_if_fail (ADW_IS_TAB_PAGE (self));

  if (!g_strcmp0 (tooltip, self->tooltip))
    return;

  g_clear_pointer (&self->tooltip, g_free);
  self->tooltip = g_strdup (tooltip ? tooltip : "");

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_TOOLTIP]);
}

/**
 * adw_tab_page_get_icon: (attributes org.gtk.Method.get_property=icon)
 * @self: a `AdwTabPage`
 *
 * Gets the icon of @self.
 *
 * Returns: (transfer none) (nullable): the icon of @self
 *
 * Since: 1.0
 */
GIcon *
adw_tab_page_get_icon (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), NULL);

  return self->icon;
}

/**
 * adw_tab_page_set_icon: (attributes org.gtk.Method.set_property=icon)
 * @self: a `AdwTabPage`
 * @icon: (nullable): the icon of @self
 *
 * Sets the icon of @self.
 *
 * Since: 1.0
 */
void
adw_tab_page_set_icon (AdwTabPage *self,
                       GIcon      *icon)
{
  g_return_if_fail (ADW_IS_TAB_PAGE (self));
  g_return_if_fail (icon == NULL || G_IS_ICON (icon));

  if (self->icon == icon)
    return;

  g_set_object (&self->icon, icon);

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_ICON]);
}

/**
 * adw_tab_page_get_loading: (attributes org.gtk.Method.get_property=loading)
 * @self: a `AdwTabPage`
 *
 * Gets whether @self is loading.
 *
 * Returns: whether @self is loading
 *
 * Since: 1.0
 */
gboolean
adw_tab_page_get_loading (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), FALSE);

  return self->loading;
}

/**
 * adw_tab_page_set_loading: (attributes org.gtk.Method.set_property=loading)
 * @self: a `AdwTabPage`
 * @loading: whether @self is loading
 *
 * Sets wether @self is loading.
 *
 * Since: 1.0
 */
void
adw_tab_page_set_loading (AdwTabPage *self,
                          gboolean    loading)
{
  g_return_if_fail (ADW_IS_TAB_PAGE (self));

  loading = !!loading;

  if (self->loading == loading)
    return;

  self->loading = loading;

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_LOADING]);
}

/**
 * adw_tab_page_get_indicator_icon: (attributes org.gtk.Method.get_property=indicator-icon)
 * @self: a `AdwTabPage`
 *
 * Gets the indicator icon of @self.
 *
 * Returns: (transfer none) (nullable): the indicator icon of @self
 *
 * Since: 1.0
 */
GIcon *
adw_tab_page_get_indicator_icon (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), NULL);

  return self->indicator_icon;
}

/**
 * adw_tab_page_set_indicator_icon: (attributes org.gtk.Method.set_property=indicator-icon)
 * @self: a `AdwTabPage`
 * @indicator_icon: (nullable): the indicator icon of @self
 *
 * Sets the indicator icon of @self.

 * Since: 1.0
 */
void
adw_tab_page_set_indicator_icon (AdwTabPage *self,
                                 GIcon      *indicator_icon)
{
  g_return_if_fail (ADW_IS_TAB_PAGE (self));
  g_return_if_fail (indicator_icon == NULL || G_IS_ICON (indicator_icon));

  if (self->indicator_icon == indicator_icon)
    return;

  g_set_object (&self->indicator_icon, indicator_icon);

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_INDICATOR_ICON]);
}

/**
 * adw_tab_page_get_indicator_activatable: (attributes org.gtk.Method.get_property=indicator-activatable)
 * @self: a `AdwTabPage`
 *
 *
 * Gets whether the indicator of @self is activatable.
 *
 * Returns: whether the indicator is activatable
 *
 * Since: 1.0
 */
gboolean
adw_tab_page_get_indicator_activatable (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), FALSE);

  return self->indicator_activatable;
}

/**
 * adw_tab_page_set_indicator_activatable: (attributes org.gtk.Method.set_property=indicator-activatable)
 * @self: a `AdwTabPage`
 * @activatable: whether the indicator is activatable
 *
 * Sets whether the indicator of @self is activatable.
 *
 * Since: 1.0
 */
void
adw_tab_page_set_indicator_activatable (AdwTabPage *self,
                                        gboolean    activatable)
{
  g_return_if_fail (ADW_IS_TAB_PAGE (self));

  activatable = !!activatable;

  if (self->indicator_activatable == activatable)
    return;

  self->indicator_activatable = activatable;

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_INDICATOR_ACTIVATABLE]);
}

/**
 * adw_tab_page_get_needs_attention: (attributes org.gtk.Method.get_property=needs-attention)
 * @self: a `AdwTabPage`
 *
 * Gets whether @self needs attention.
 *
 * Returns: whether @self needs attention
 *
 * Since: 1.0
 */
gboolean
adw_tab_page_get_needs_attention (AdwTabPage *self)
{
  g_return_val_if_fail (ADW_IS_TAB_PAGE (self), FALSE);

  return self->needs_attention;
}

/**
 * adw_tab_page_set_needs_attention: (attributes org.gtk.Method.set_property=needs-attention)
 * @self: a `AdwTabPage`
 * @needs_attention: whether @self needs attention
 *
 * Sets whether @self needs attention.
 *
 * Since: 1.0
 */
void
adw_tab_page_set_needs_attention (AdwTabPage *self,
                                  gboolean    needs_attention)
{
  g_return_if_fail (ADW_IS_TAB_PAGE (self));

  needs_attention = !!needs_attention;

  if (self->needs_attention == needs_attention)
    return;

  self->needs_attention = needs_attention;

  g_object_notify_by_pspec (G_OBJECT (self), page_props[PAGE_PROP_NEEDS_ATTENTION]);
}

/**
 * adw_tab_view_new:
 *
 * Creates a new `AdwTabView`.
 *
 * Returns: the newly created `AdwTabView`
 *
 * Since: 1.0
 */
AdwTabView *
adw_tab_view_new (void)
{
  return g_object_new (ADW_TYPE_TAB_VIEW, NULL);
}

/**
 * adw_tab_view_get_n_pages: (attributes org.gtk.Method.get_property=n-pages)
 * @self: a `AdwTabView`
 *
 * Gets the number of pages in @self.
 *
 * Returns: the number of pages in @self
 *
 * Since: 1.0
 */
int
adw_tab_view_get_n_pages (AdwTabView *self)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), 0);

  return self->n_pages;
}

/**
 * adw_tab_view_get_n_pinned_pages: (attributes org.gtk.Method.get_property=n-pinned-pages)
 * @self: a `AdwTabView`
 *
 * Gets the number of pinned pages in @self.
 *
 * Returns: the number of pinned pages in @self
 *
 * Since: 1.0
 */
int
adw_tab_view_get_n_pinned_pages (AdwTabView *self)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), 0);

  return self->n_pinned_pages;
}

/**
 * adw_tab_view_get_is_transferring_page: (attributes org.gtk.Method.get_property=is-transferring-page)
 * @self: a `AdwTabView`
 *
 * Whether a page is being transferred.
 *
 * Returns: whether a page is being transferred
 *
 * Since: 1.0
 */
gboolean
adw_tab_view_get_is_transferring_page (AdwTabView *self)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), FALSE);

  return self->transfer_count > 0;
}

/**
 * adw_tab_view_get_selected_page: (attributes org.gtk.Method.get_property=selected-page)
 * @self: a `AdwTabView`
 *
 * Gets the currently selected page in @self.
 *
 * Returns: (transfer none) (nullable): the selected page
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_view_get_selected_page (AdwTabView *self)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);

  return self->selected_page;
}

/**
 * adw_tab_view_set_selected_page: (attributes org.gtk.Method.set_property=selected-page)
 * @self: a `AdwTabView`
 * @selected_page: a page in @self
 *
 * Sets the currently selected page in @self.
 *
 * Since: 1.0
 */
void
adw_tab_view_set_selected_page (AdwTabView *self,
                                AdwTabPage *selected_page)
{
  g_return_if_fail (ADW_IS_TAB_VIEW (self));

  if (self->n_pages > 0) {
    g_return_if_fail (ADW_IS_TAB_PAGE (selected_page));
    g_return_if_fail (page_belongs_to_this_view (self, selected_page));
  } else {
    g_return_if_fail (selected_page == NULL);
  }

  set_selected_page (self, selected_page, TRUE);
}

/**
 * adw_tab_view_select_previous_page:
 * @self: a `AdwTabView`
 *
 * Selects the page before the currently selected page.
 *
 * If the first page was already selected, this function does nothing.
 *
 * Returns: whether the selected page was changed
 *
 * Since: 1.0
 */
gboolean
adw_tab_view_select_previous_page (AdwTabView *self)
{
  AdwTabPage *page;
  int pos;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), FALSE);

  if (!self->selected_page)
    return FALSE;

  pos = adw_tab_view_get_page_position (self, self->selected_page);

  if (pos <= 0)
    return FALSE;

  page = adw_tab_view_get_nth_page (self, pos - 1);

  adw_tab_view_set_selected_page (self, page);

  return TRUE;
}

/**
 * adw_tab_view_select_next_page:
 * @self: a `AdwTabView`
 *
 * Selects the page after the currently selected page.
 *
 * If the last page was already selected, this function does nothing.
 *
 * Returns: whether the selected page was changed
 *
 * Since: 1.0
 */
gboolean
adw_tab_view_select_next_page (AdwTabView *self)
{
  AdwTabPage *page;
  int pos;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), FALSE);

  if (!self->selected_page)
    return FALSE;

  pos = adw_tab_view_get_page_position (self, self->selected_page);

  if (pos >= self->n_pages - 1)
    return FALSE;

  page = adw_tab_view_get_nth_page (self, pos + 1);

  adw_tab_view_set_selected_page (self, page);

  return TRUE;
}

gboolean
adw_tab_view_select_first_page (AdwTabView *self)
{
  AdwTabPage *page;
  int pos;
  gboolean pinned;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), FALSE);

  if (!self->selected_page)
    return FALSE;

  pinned = adw_tab_page_get_pinned (self->selected_page);
  pos = pinned ? 0 : self->n_pinned_pages;

  page = adw_tab_view_get_nth_page (self, pos);

  /* If we're on the first non-pinned tab, go to the first pinned tab */
  if (page == self->selected_page && !pinned)
    page = adw_tab_view_get_nth_page (self, 0);

  if (page == self->selected_page)
    return FALSE;

  adw_tab_view_set_selected_page (self, page);

  return TRUE;
}

gboolean
adw_tab_view_select_last_page (AdwTabView *self)
{
  AdwTabPage *page;
  int pos;
  gboolean pinned;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), FALSE);

  if (!self->selected_page)
    return FALSE;

  pinned = adw_tab_page_get_pinned (self->selected_page);
  pos = (pinned ? self->n_pinned_pages : self->n_pages) - 1;

  page = adw_tab_view_get_nth_page (self, pos);

  /* If we're on the last pinned tab, go to the last non-pinned tab */
  if (page == self->selected_page && pinned)
    page = adw_tab_view_get_nth_page (self, self->n_pages - 1);

  if (page == self->selected_page)
    return FALSE;

  adw_tab_view_set_selected_page (self, page);

  return TRUE;
}

/**
 * adw_tab_view_get_default_icon: (attributes org.gtk.Method.get_property=default-icon)
 * @self: a `AdwTabView`
 *
 * Gets the default icon of @self.
 *
 * Returns: (transfer none): the default icon of @self.
 *
 * Since: 1.0
 */
GIcon *
adw_tab_view_get_default_icon (AdwTabView *self)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);

  return self->default_icon;
}

/**
 * adw_tab_view_set_default_icon: (attributes org.gtk.Method.set_property=default-icon)
 * @self: a `AdwTabView`
 * @default_icon: the default icon
 *
 * Sets the default page icon for @self.
 *
 * Since: 1.0
 */
void
adw_tab_view_set_default_icon (AdwTabView *self,
                               GIcon      *default_icon)
{
  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (G_IS_ICON (default_icon));

  if (self->default_icon == default_icon)
    return;

  g_set_object (&self->default_icon, default_icon);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DEFAULT_ICON]);
}

/**
 * adw_tab_view_get_menu_model: (attributes org.gtk.Method.get_property=menu-model)
 * @self: a `AdwTabView`
 *
 * Gets the tab context menu model for @self.
 *
 * Returns: (transfer none) (nullable): the tab context menu model for @self
 *
 * Since: 1.0
 */
GMenuModel *
adw_tab_view_get_menu_model (AdwTabView *self)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);

  return self->menu_model;
}

/**
 * adw_tab_view_set_menu_model: (attributes org.gtk.Method.set_property=menu-model)
 * @self: a `AdwTabView`
 * @menu_model: (nullable): a menu model
 *
 * Sets the tab context menu model for @self.
 *
 * Since: 1.0
 */
void
adw_tab_view_set_menu_model (AdwTabView *self,
                             GMenuModel *menu_model)
{
  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (G_IS_MENU_MODEL (menu_model));

  if (self->menu_model == menu_model)
    return;

  g_set_object (&self->menu_model, menu_model);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MENU_MODEL]);
}

/**
 * adw_tab_view_get_shortcut_widget: (attributes org.gtk.Method.get_property=shortcut-widget)
 * @self: a `AdwTabView`
 *
 * Gets the shortcut widget for @self.
 *
 * Returns: (transfer none) (nullable): the shortcut widget for @self
 *
 * Since: 1.0
 */
GtkWidget *
adw_tab_view_get_shortcut_widget (AdwTabView *self)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);

  return self->shortcut_widget;
}

/**
 * adw_tab_view_set_shortcut_widget: (attributes org.gtk.Method.set_property=shortcut-widget)
 * @self: a `AdwTabView`
 * @widget: (nullable): a shortcut widget
 *
 * Sets the shortcut widget for @self.
 *
 * Since: 1.0
 */
void
adw_tab_view_set_shortcut_widget (AdwTabView *self,
                                  GtkWidget  *widget)
{
  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (widget == NULL || GTK_IS_WIDGET (widget));

  if (widget == self->shortcut_widget)
    return;

  if (self->shortcut_widget) {
    gtk_widget_remove_controller (self->shortcut_widget, self->shortcut_controller);
    self->shortcut_controller = NULL;

    g_object_weak_unref (G_OBJECT (self->shortcut_widget),
                         (GWeakNotify) shortcut_widget_notify_cb,
                         self);
  }

  self->shortcut_widget = widget;

  if (self->shortcut_widget) {
    g_object_weak_ref (G_OBJECT (self->shortcut_widget),
                       (GWeakNotify) shortcut_widget_notify_cb,
                       self);

    self->shortcut_controller = gtk_shortcut_controller_new ();

    init_shortcuts (self, self->shortcut_controller);

    gtk_widget_add_controller (self->shortcut_widget, self->shortcut_controller);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHORTCUT_WIDGET]);
}

/**
 * adw_tab_view_set_page_pinned:
 * @self: a `AdwTabView`
 * @page: a page of @self
 * @pinned: whether @page should be pinned
 *
 * Pins or unpins @page.
 *
 * Pinned pages are guaranteed to be placed before all non-pinned pages; at any
 * given moment the first [property@Adw.TabView:n-pinned-pages] pages in @self
 * are guaranteed to be pinned.
 *
 * When a page is pinned or unpinned, it's automatically reordered: pinning a
 * page moves it after other pinned pages; unpinning a page moves it before
 * other non-pinned pages.
 *
 * Pinned pages can still be reordered between each other.
 *
 * [class@Adw.TabBar] will display pinned pages in a compact form, never showing
 * the title or close button, and only showing a single icon, selected in the
 * following order:
 *
 * 1. [property@Adw.TabPage:indicator-icon]
 * 2. A spinner if [property@Adw.TabPage:loading] is `TRUE`
 * 3. [property@Adw.TabPage:icon]
 * 4. [property@Adw.TabView:default-icon]
 *
 * Pinned pages cannot be closed by default, see
 * [signal@Adw.TabView::close-page] for how to override that behavior.
 *
 * Changes the value of the [property@Adw.TabPage:pinned] property.
 *
 * Since: 1.0
 */
void
adw_tab_view_set_page_pinned (AdwTabView *self,
                              AdwTabPage *page,
                              gboolean    pinned)
{
  int old_pos, new_pos;

  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (ADW_IS_TAB_PAGE (page));
  g_return_if_fail (page_belongs_to_this_view (self, page));

  pinned = !!pinned;

  if (adw_tab_page_get_pinned (page) == pinned)
    return;

  old_pos = adw_tab_view_get_page_position (self, page);

  g_object_ref (page);

  g_list_store_remove (self->children, old_pos);

  new_pos = self->n_pinned_pages;

  if (!pinned)
    new_pos--;

  g_list_store_insert (self->children, new_pos, page);

  g_object_unref (page);

  set_n_pinned_pages (self, new_pos + (pinned ? 1 : 0));
  set_page_pinned (page, pinned);

  if (self->pages) {
    int min = MIN (old_pos, new_pos);
    int n_changed = MAX (old_pos, new_pos) - min + 1;

    g_list_model_items_changed (G_LIST_MODEL (self->pages), min, n_changed, n_changed);
  }
}

/**
 * adw_tab_view_get_page:
 * @self: a `AdwTabView`
 * @child: a child in @self
 *
 * Gets the [class@Adw.TabPage] object representing @child.
 *
 * Returns: (transfer none): the page object for @child
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_view_get_page (AdwTabView *self,
                       GtkWidget  *child)
{
  int i;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (child), NULL);
  g_return_val_if_fail (gtk_widget_get_parent (child) == GTK_WIDGET (self->stack), NULL);

  for (i = 0; i < self->n_pages; i++) {
    AdwTabPage *page = adw_tab_view_get_nth_page (self, i);

    if (adw_tab_page_get_child (page) == child)
      return page;
  }

  g_assert_not_reached ();
}

/**
 * adw_tab_view_get_nth_page:
 * @self: a `AdwTabView`
 * @position: the index of the page in @self, starting from 0
 *
 * Gets the [class@Adw.TabPage] representing the child at @position.
 *
 * Returns: (transfer none): the page object at @position
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_view_get_nth_page (AdwTabView *self,
                           int         position)
{
  g_autoptr (AdwTabPage) page = NULL;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (position >= 0, NULL);
  g_return_val_if_fail (position < self->n_pages, NULL);

  page = g_list_model_get_item (G_LIST_MODEL (self->children), (guint) position);

  return page;
}

/**
 * adw_tab_view_get_page_position:
 * @self: a `AdwTabView`
 * @page: a page of @self
 *
 * Finds the position of @page in @self, starting from 0.
 *
 * Returns: the position of @page in @self
 *
 * Since: 1.0
 */
int
adw_tab_view_get_page_position (AdwTabView *self,
                                AdwTabPage *page)
{
  int i;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), -1);
  g_return_val_if_fail (ADW_IS_TAB_PAGE (page), -1);
  g_return_val_if_fail (page_belongs_to_this_view (self, page), -1);

  for (i = 0; i < self->n_pages; i++) {
    AdwTabPage *p = adw_tab_view_get_nth_page (self, i);

    if (page == p)
      return i;
  }

  g_assert_not_reached ();
}

/**
 * adw_tab_view_add_page:
 * @self: a `AdwTabView`
 * @child: a widget to add
 * @parent: (nullable): a parent page for @child
 *
 * Adds @child to @self with @parent as the parent.
 *
 * This function can be used to automatically position new pages, and to select
 * the correct page when this page is closed while being selected (see
 * [method@Adw.TabView.close_page]).
 *
 * If @parent is `NULL`, this function is equivalent to
 * [method@Adw.TabView.append].
 *
 * Returns: (transfer none): the page object representing @child
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_view_add_page (AdwTabView *self,
                       GtkWidget  *child,
                       AdwTabPage *parent)
{
  int position;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (child), NULL);
  g_return_val_if_fail (parent == NULL || ADW_IS_TAB_PAGE (parent), NULL);

  if (parent) {
    AdwTabPage *page;

    g_return_val_if_fail (page_belongs_to_this_view (self, parent), NULL);

    if (adw_tab_page_get_pinned (parent))
      position = self->n_pinned_pages - 1;
    else
      position = adw_tab_view_get_page_position (self, parent);

    do {
      position++;

      if (position >= self->n_pages)
        break;

      page = adw_tab_view_get_nth_page (self, position);
    } while (is_descendant_of (page, parent));
  } else {
    position = self->n_pages;
  }

  return insert_page (self, child, parent, position, FALSE);
}

/**
 * adw_tab_view_insert:
 * @self: a `AdwTabView`
 * @child: a widget to add
 * @position: the position to add @child at, starting from 0
 *
 * Inserts a non-pinned page at @position.
 *
 * It's an error to try to insert a page before a pinned page, in that case
 * [method@Adw.TabView.insert_pinned] should be used instead.
 *
 * Returns: (transfer none): the page object representing @child
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_view_insert (AdwTabView *self,
                     GtkWidget  *child,
                     int         position)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (child), NULL);
  g_return_val_if_fail (position >= self->n_pinned_pages, NULL);
  g_return_val_if_fail (position <= self->n_pages, NULL);

  return insert_page (self, child, NULL, position, FALSE);
}

/**
 * adw_tab_view_prepend:
 * @self: a `AdwTabView`
 * @child: a widget to add
 *
 * Inserts @child as the first non-pinned page.
 *
 * Returns: (transfer none): the page object representing @child
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_view_prepend (AdwTabView *self,
                      GtkWidget  *child)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (child), NULL);

  return insert_page (self, child, NULL, self->n_pinned_pages, FALSE);
}

/**
 * adw_tab_view_append:
 * @self: a `AdwTabView`
 * @child: a widget to add
 *
 * Inserts @child as the last non-pinned page.
 *
 * Returns: (transfer none): the page object representing @child
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_view_append (AdwTabView *self,
                     GtkWidget  *child)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (child), NULL);

  return insert_page (self, child, NULL, self->n_pages, FALSE);
}

/**
 * adw_tab_view_insert_pinned:
 * @self: a `AdwTabView`
 * @child: a widget to add
 * @position: the position to add @child at, starting from 0
 *
 * Inserts a pinned page at @position.
 *
 * It's an error to try to insert a pinned page after a non-pinned page, in
 * that case [method@Adw.TabView.insert] should be used instead.
 *
 * Returns: (transfer none): the page object representing @child
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_view_insert_pinned (AdwTabView *self,
                            GtkWidget  *child,
                            int         position)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (child), NULL);
  g_return_val_if_fail (position >= 0, NULL);
  g_return_val_if_fail (position <= self->n_pinned_pages, NULL);

  return insert_page (self, child, NULL, position, TRUE);
}

/**
 * adw_tab_view_prepend_pinned:
 * @self: a `AdwTabView`
 * @child: a widget to add
 *
 * Inserts @child as the first pinned page.
 *
 * Returns: (transfer none): the page object representing @child
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_view_prepend_pinned (AdwTabView *self,
                             GtkWidget  *child)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (child), NULL);

  return insert_page (self, child, NULL, 0, TRUE);
}

/**
 * adw_tab_view_append_pinned:
 * @self: a `AdwTabView`
 * @child: a widget to add
 *
 * Inserts @child as the last pinned page.
 *
 * Returns: (transfer none): the page object representing @child
 *
 * Since: 1.0
 */
AdwTabPage *
adw_tab_view_append_pinned (AdwTabView *self,
                            GtkWidget  *child)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (child), NULL);

  return insert_page (self, child, NULL, self->n_pinned_pages, TRUE);
}

/**
 * adw_tab_view_close_page:
 * @self: a `AdwTabView`
 * @page: a page of @self
 *
 * Requests to close @page.
 *
 * Calling this function will result in the [signal@Adw.TabView::close-page]
 * signal being emitted for @page. Closing the page can then be confirmed or
 * denied via [method@Adw.TabView.close_page_finish].
 *
 * If the page is waiting for a [method@Adw.TabView.close_page_finish] call,
 * this function will do nothing.
 *
 * The default handler for [signal@Adw.TabView::close-page] will immediately
 * confirm closing the page if it's non-pinned, or reject it if it's pinned.
 * This behavior can be changed by registering your own handler for that signal.
 *
 * If @page was selected, another page will be selected instead:
 *
 * If the [property@Adw.TabPage:parent] value is `NULL`, the next page will be
 * selected when possible, or if the page was already last, the previous page
 * will be selected instead.
 *
 * If it's not `NULL`, the previous page will be selected if it's a descendant
 * (possibly indirect) of the parent. If both the previous page and the parent
 * are pinned, the parent will be selected instead.
 *
 * Since: 1.0
 */
void
adw_tab_view_close_page (AdwTabView *self,
                         AdwTabPage *page)
{
  gboolean ret;

  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (ADW_IS_TAB_PAGE (page));
  g_return_if_fail (page_belongs_to_this_view (self, page));

  if (page->closing)
    return;

  page->closing = TRUE;
  g_signal_emit (self, signals[SIGNAL_CLOSE_PAGE], 0, page, &ret);
}

/**
 * adw_tab_view_close_page_finish:
 * @self: a `AdwTabView`
 * @page: a page of @self
 * @confirm: whether to confirm or deny closing @page
 *
 * Completes a [method@Adw.TabView.close_page] call for @page.
 *
 * If @confirm is `TRUE`, @page will be closed. If it's `FALSE`, it will be
 * reverted to its previous state and [method@Adw.TabView.close_page] can be
 * called for it again.
 *
 * This function should not be called unless a custom handler for
 * [signal@Adw.TabView::close-page] is used.
 *
 * Since: 1.0
 */
void
adw_tab_view_close_page_finish (AdwTabView *self,
                                AdwTabPage *page,
                                gboolean    confirm)
{
  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (ADW_IS_TAB_PAGE (page));
  g_return_if_fail (page_belongs_to_this_view (self, page));
  g_return_if_fail (page->closing);

  page->closing = FALSE;

  if (confirm)
    detach_page (self, page, TRUE);
}

/**
 * adw_tab_view_close_other_pages:
 * @self: a `AdwTabView`
 * @page: a page of @self
 *
 * Requests to close all pages other than @page.
 *
 * Since: 1.0
 */
void
adw_tab_view_close_other_pages (AdwTabView *self,
                                AdwTabPage *page)
{
  int i;

  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (ADW_IS_TAB_PAGE (page));
  g_return_if_fail (page_belongs_to_this_view (self, page));

  for (i = self->n_pages - 1; i >= 0; i--) {
    AdwTabPage *p = adw_tab_view_get_nth_page (self, i);

    if (p == page)
      continue;

    adw_tab_view_close_page (self, p);
  }
}

/**
 * adw_tab_view_close_pages_before:
 * @self: a `AdwTabView`
 * @page: a page of @self
 *
 * Requests to close all pages before @page.
 *
 * Since: 1.0
 */
void
adw_tab_view_close_pages_before (AdwTabView *self,
                                 AdwTabPage *page)
{
  int pos, i;

  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (ADW_IS_TAB_PAGE (page));
  g_return_if_fail (page_belongs_to_this_view (self, page));

  pos = adw_tab_view_get_page_position (self, page);

  for (i = pos - 1; i >= 0; i--) {
    AdwTabPage *p = adw_tab_view_get_nth_page (self, i);

    adw_tab_view_close_page (self, p);
  }
}

/**
 * adw_tab_view_close_pages_after:
 * @self: a `AdwTabView`
 * @page: a page of @self
 *
 * Requests to close all pages after @page.
 *
 * Since: 1.0
 */
void
adw_tab_view_close_pages_after (AdwTabView *self,
                                AdwTabPage *page)
{
  int pos, i;

  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (ADW_IS_TAB_PAGE (page));
  g_return_if_fail (page_belongs_to_this_view (self, page));

  pos = adw_tab_view_get_page_position (self, page);

  for (i = self->n_pages - 1; i > pos; i--) {
    AdwTabPage *p = adw_tab_view_get_nth_page (self, i);

    adw_tab_view_close_page (self, p);
  }
}

/**
 * adw_tab_view_reorder_page:
 * @self: a `AdwTabView`
 * @page: a page of @self
 * @position: the position to insert the page at, starting at 0
 *
 * Reorders @page to @position.
 *
 * It's a programmer error to try to reorder a pinned page after a non-pinned
 * one, or a non-pinned page before a pinned one.
 *
 * Returns: whether @page was moved
 *
 * Since: 1.0
 */
gboolean
adw_tab_view_reorder_page (AdwTabView *self,
                           AdwTabPage *page,
                           int         position)
{
  int original_pos;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (ADW_IS_TAB_PAGE (page), FALSE);
  g_return_val_if_fail (page_belongs_to_this_view (self, page), FALSE);

  if (adw_tab_page_get_pinned (page)) {
    g_return_val_if_fail (position >= 0, FALSE);
    g_return_val_if_fail (position < self->n_pinned_pages, FALSE);
  } else {
    g_return_val_if_fail (position >= self->n_pinned_pages, FALSE);
    g_return_val_if_fail (position < self->n_pages, FALSE);
  }

  original_pos = adw_tab_view_get_page_position (self, page);

  if (original_pos == position)
    return FALSE;

  g_object_ref (page);

  g_list_store_remove (self->children, original_pos);
  g_list_store_insert (self->children, position, page);

  g_object_unref (page);

  g_signal_emit (self, signals[SIGNAL_PAGE_REORDERED], 0, page, position);

  if (self->pages) {
    int min = MIN (original_pos, position);
    int n_changed = MAX (original_pos, position) - min + 1;

    g_list_model_items_changed (G_LIST_MODEL (self->pages), min, n_changed, n_changed);
  }

  return TRUE;
}

/**
 * adw_tab_view_reorder_backward:
 * @self: a `AdwTabView`
 * @page: a page of @self
 *
 * Reorders @page to before its previous page if possible.
 *
 * Returns: whether @page was moved
 *
 * Since: 1.0
 */
gboolean
adw_tab_view_reorder_backward (AdwTabView *self,
                               AdwTabPage *page)
{
  gboolean pinned;
  int pos, first;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (ADW_IS_TAB_PAGE (page), FALSE);
  g_return_val_if_fail (page_belongs_to_this_view (self, page), FALSE);

  pos = adw_tab_view_get_page_position (self, page);

  pinned = adw_tab_page_get_pinned (page);
  first = pinned ? 0 : self->n_pinned_pages;

  if (pos <= first)
    return FALSE;

  return adw_tab_view_reorder_page (self, page, pos - 1);
}

/**
 * adw_tab_view_reorder_forward:
 * @self: a `AdwTabView`
 * @page: a page of @self
 *
 * Reorders @page to after its next page if possible.
 *
 * Returns: whether @page was moved
 *
 * Since: 1.0
 */
gboolean
adw_tab_view_reorder_forward (AdwTabView *self,
                              AdwTabPage *page)
{
  gboolean pinned;
  int pos, last;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (ADW_IS_TAB_PAGE (page), FALSE);
  g_return_val_if_fail (page_belongs_to_this_view (self, page), FALSE);

  pos = adw_tab_view_get_page_position (self, page);

  pinned = adw_tab_page_get_pinned (page);
  last = (pinned ? self->n_pinned_pages : self->n_pages) - 1;

  if (pos >= last)
    return FALSE;

  return adw_tab_view_reorder_page (self, page, pos + 1);
}

/**
 * adw_tab_view_reorder_first:
 * @self: a `AdwTabView`
 * @page: a page of @self
 *
 * Reorders @page to the first possible position.
 *
 * Returns: whether @page was moved
 *
 * Since: 1.0
 */
gboolean
adw_tab_view_reorder_first (AdwTabView *self,
                            AdwTabPage *page)
{
  gboolean pinned;
  int pos;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (ADW_IS_TAB_PAGE (page), FALSE);
  g_return_val_if_fail (page_belongs_to_this_view (self, page), FALSE);

  pinned = adw_tab_page_get_pinned (page);
  pos = pinned ? 0 : self->n_pinned_pages;

  return adw_tab_view_reorder_page (self, page, pos);
}

/**
 * adw_tab_view_reorder_last:
 * @self: a `AdwTabView`
 * @page: a page of @self
 *
 * Reorders @page to the last possible position.
 *
 * Returns: whether @page was moved
 *
 * Since: 1.0
 */
gboolean
adw_tab_view_reorder_last (AdwTabView *self,
                           AdwTabPage *page)
{
  gboolean pinned;
  int pos;

  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), FALSE);
  g_return_val_if_fail (ADW_IS_TAB_PAGE (page), FALSE);
  g_return_val_if_fail (page_belongs_to_this_view (self, page), FALSE);

  pinned = adw_tab_page_get_pinned (page);
  pos = (pinned ? self->n_pinned_pages : self->n_pages) - 1;

  return adw_tab_view_reorder_page (self, page, pos);
}

void
adw_tab_view_detach_page (AdwTabView *self,
                          AdwTabPage *page)
{
  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (ADW_IS_TAB_PAGE (page));
  g_return_if_fail (page_belongs_to_this_view (self, page));

  g_object_ref (page);

  begin_transfer_for_group (self);

  detach_page (self, page, TRUE);
}

void
adw_tab_view_attach_page (AdwTabView *self,
                          AdwTabPage *page,
                          int         position)
{
  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (ADW_IS_TAB_PAGE (page));
  g_return_if_fail (!page_belongs_to_this_view (self, page));
  g_return_if_fail (position >= 0);
  g_return_if_fail (position <= self->n_pages);

  attach_page (self, page, position);

  if (self->pages)
    g_list_model_items_changed (G_LIST_MODEL (self->pages), position, 0, 1);

  adw_tab_view_set_selected_page (self, page);

  end_transfer_for_group (self);

  g_object_unref (page);
}

/**
 * adw_tab_view_transfer_page:
 * @self: a `AdwTabView`
 * @page: a page of @self
 * @other_view: the tab view to transfer the page to
 * @position: the position to insert the page at, starting at 0
 *
 * Transfers @page from @self to @other_view.
 *
 * The @page object will be reused.
 *
 * It's a programmer error to try to insert a pinned page after a non-pinned
 * one, or a non-pinned page before a pinned one.
 *
 * Since: 1.0
 */
void
adw_tab_view_transfer_page (AdwTabView *self,
                            AdwTabPage *page,
                            AdwTabView *other_view,
                            int         position)
{
  gboolean pinned;

  g_return_if_fail (ADW_IS_TAB_VIEW (self));
  g_return_if_fail (ADW_IS_TAB_PAGE (page));
  g_return_if_fail (ADW_IS_TAB_VIEW (other_view));
  g_return_if_fail (page_belongs_to_this_view (self, page));
  g_return_if_fail (position >= 0);
  g_return_if_fail (position <= other_view->n_pages);

  pinned = adw_tab_page_get_pinned (page);

  g_return_if_fail (!pinned || position <= other_view->n_pinned_pages);
  g_return_if_fail (pinned || position >= other_view->n_pinned_pages);

  adw_tab_view_detach_page (self, page);
  adw_tab_view_attach_page (other_view, page, position);
}

/**
 * adw_tab_view_get_pages: (attributes org.gtk.Method.get_property=pages)
 * @self: a `AdwTabView`
 *
 * Returns a [iface@Gio.ListModel] that contains the pages of @self.
 *
 * This can be used to keep an up-to-date view. The model also implements
 * [iface@Gtk.SelectionModel] and can be used to track and change the selected
 * page.
 *
 * Returns: (transfer full): a `GtkSelectionModel` for the pages of @self
 *
 * Since: 1.0
 */
GtkSelectionModel *
adw_tab_view_get_pages (AdwTabView *self)
{
  g_return_val_if_fail (ADW_IS_TAB_VIEW (self), NULL);

  if (self->pages)
    return g_object_ref (self->pages);

  self->pages = adw_tab_pages_new (self);
  g_object_add_weak_pointer (G_OBJECT (self->pages),
                             (gpointer *) &self->pages);

  return self->pages;
}

AdwTabView *
adw_tab_view_create_window (AdwTabView *self)
{
  AdwTabView *new_view = NULL;

  g_signal_emit (self, signals[SIGNAL_CREATE_WINDOW], 0, &new_view);

  if (!new_view) {
    g_critical ("AdwTabView::create-window handler must not return NULL");

    return NULL;
  }

  new_view->transfer_count = self->transfer_count;

  return new_view;
}
