Title: Style Classes
Slug: style-classes

# Style Classes

The Adwaita stylesheet provides a number of style classes. They can be applied
to widgets to change their appearance.

## Buttons

The following style classes can be applied to [class@Gtk.Button] to change its
appearance.

### Suggested Action

The `.suggested-action` style class makes the button use accent colors. It can
be used to denote important buttons, for example, the affirmative button in an
action dialog.

It can be used in combination with [`.circular`](#circular) or [`.pill`](#pill).

Can also be used with [class@Gtk.MenuButton] or [class@Adw.SplitButton].

### Destructive Action

The `.destructive-action` style class makes the button use destructive colors.
It can be used to draw attention to the potentially damaging consequences of
using a button. This style acts as a warning to the user.

It can be used in combination with [`.circular`](#circular) or [`.pill`](#pill).

Can also be used with [class@Gtk.MenuButton] or [class@Adw.SplitButton].

### Flat

The `.flat` style class makes the button use flat appearance, looking like a
label or an icon until hovered.

Button inside [toolbars and similar widgets](#toolbars) appear flat by default.

It can be used in combination with [`.circular`](#circular) or [`.pill`](#pill).

Can also be used with [class@Gtk.MenuButton] or [class@Adw.SplitButton].

Can be set via [property@Gtk.Button:has-frame] and
[property@Gtk.MenuButton:has-frame].

### Raised

The `.raised` style class makes the button use the regular appearance instead of
the flat one.

This style class is only useful inside [toolbars and similar widgets](#toolbars).

It can be used in combination with [`.circular`](#circular) or [`.pill`](#pill).

Can also be used with [class@Gtk.MenuButton] or [class@Adw.SplitButton].

### Opaque

The `.opaque` style class gives the button an opaque background. It's intended
to be used together with custom styles that override `background-color` and
`color`, to create buttons with an appearance similar to
[`.suggested-action`](#suggested-action) and
[`.destructive-action`](#destructive-action), but with custom colors.

For example, `.suggested-action` and `.destructive-action` are equivalent to
using the `.opaque` style
class with the following CSS:

```css
#custom-suggested-action-button {
  background-color: @accent_bg_color;
  color: @accent_fg_color;
}

#custom-destructive-action-button {
  background-color: @destructive_bg_color;
  color: @destructive_fg_color;
}
```

It can be used in combination with [`.circular`](#circular) or [`.pill`](#pill).

Can also be used with [class@Gtk.MenuButton] or [class@Adw.SplitButton].

### Circular

The `.circular` style class makes the button round. It can be used with buttons
containing icons or short labels (1-2 characters).

It can be used in combination with [`.suggested-action`](#suggested-action),
[`.destructive-action`](#destructive-action), [`.flat`](#flat),
[`.raised`](#raised), [`.opaque`](#opaque) or [`.osd`](#overlay-buttons).

Can also be used with [class@Gtk.MenuButton].

### Pill

The `.pill` style class makes the button appear as a pill. It's often used for
important standalone buttons, for example, inside a [class@Adw.StatusPage].

It can be used in combination with [`.suggested-action`](#suggested-action),
[`.destructive-action`](#destructive-action), [`.flat`](#flat),
[`.raised`](#raised), [`.opaque`](#opaque) or [`.osd`](#overlay-buttons).

## Linked Controls

The `.linked` style class can be applied to a [class@Gtk.Box] to make its
children appear as a single group. The box must have no spacing.

The following widgets can be linked:

* [class@Gtk.AppChooserButton]
* [class@Gtk.Button]
* [class@Gtk.Entry]
* [class@Gtk.ColorButton]
* [class@Gtk.ComboBox]
* [class@Gtk.DropDown]
* [class@Gtk.FontButton]
* [class@Gtk.MenuButton]
* [class@Gtk.PasswordEntry]
* [class@Gtk.SearchEntry]
* [class@Gtk.SpinButton]

Linked styles will not work correctly for buttons with the following style
classes:

* [`.flat`](#flat)
* [`.suggested-action`](#suggested-action)
* [`.destructive-action`](#destructive-action)
* [`.opaque`](#opaque)

If a linked box is contained within a [toolbar or a similar widget](#toolbars),
buttons inside it won't get the flat appearance.

## Toolbars

The `.toolbar` style class can be applied to a horizontal [class@Gtk.Box]. The
same appearance is also used by [class@Gtk.HeaderBar], [class@Gtk.ActionBar] and
[class@Gtk.SearchBar] automatically.

It changes the appearance of buttons inside it to make them flat when possible,
according to the following rules:

The following buttons get flat appearance:

* Icon-only buttons;
* Buttons with an icon and a label (using [class@Adw.ButtonContent]);
* Menu buttons containing an arrow;
* [class@Adw.SplitButton];
* Any other button with the [`.flat`](#flat) style class.

The following buttons keep default appearance:

* Text-only buttons;
* Buttons with other content;
* Buttons within widgets with the [`.linked`](#linked-controls) style
  class;
* Buttons with the [`.suggested-action`](#suggested-action) or
  [`.destructive-action`](#destructive-action) style classes.
* Buttons with the [`.raised`](#raised) style class.

It also ensures 6px margins and spacing between widgets. The
[`.spacer`](#spacers) style class can be useful to separate groups of widgets.

Important: the [property@Gtk.Button:has-frame] property will **not** be set to
`FALSE` when a button gets the flat appearance automatically. It also cannot be
set to `TRUE` to make a button raised, the style class should be used directly
instead.

## Spacers

The `.spacer` style class can be applied to a [class@Gtk.Separator] to make it
appear invisible and act as whitespace. This can be useful with [toolbars and
similar widgets](#toolbars) to separate groups of widgets from each other.

## Dim Labels

The `.dim-label` style class makes the widget it's applied to partially
transparent.

The level of transparency differs between regular and high contrast styles. As
such, it's highly recommended to be used instead of changing opacity manually.

## Typography Styles

These style classes can be applied to any widgets, but are mostly used for
[class@Gtk.Label] or other widgets that contain them.

The `.large-title` style class makes text large and thin. It's the largest
style, infrequently used for display headings in greeters or assistants. It
should only be used in conjunction with large amounts of whitespace.

The `.title-1`, `.title-2`, `.title-3`, `.title-4` classes provide four levels
of title styles, indicating hierarchy. The specific use heavily depends on
context. Generally, the larger styles are intended to be used in bigger views
with plenty of whitespace around them.

The `.heading` style class is the standard style for UI headings using the
default text size, such as window titles or boxed list labels.

The `.body` style class is the default text style.

The `.caption-heading` and `.caption` style classes make text smaller. They
are intended to be used to differentiate sub-text which accompanies text in
the regular body style.

The `.monospace` style class makes the widget use a monospace font. This can be
useful when displaying code, logs or shell commands.

The `.numeric` style class makes the widget use tabular figures. This is
equivalent to using [struct@Pango.AttrFontFeatures] with `"tnum=1"` features.
This style is useful in situations where multiple labels are vertically aligned,
or when displaying time, an operation progress or another number that can
quickly change.

## Colors

The following style classes change widget colors:

Class             | Color
----------------- | -------------------------------------------
<tt>.accent</tt>  | [accent color](named-colors.html#accent-colors)
<tt>.success</tt> | [success color](named-colors.html#success-colors)
<tt>.warning</tt> | [warning color](named-colors.html#warning-colors)
<tt>.error</tt>   | [error color](named-colors.html#error-colors)

They can be applied to any widget.

The `.error`, `.warning` and `.success` style classes can be applied to
[class@Gtk.Entry]. In that case, they can be used to indicate input validation
state.

## Boxed Lists & Cards

The `.boxed-list` style class can be applied to a [class@Gtk.ListBox] to make it
a boxed list. The list box should have [property@Gtk.ListBox:selection-mode] set
to `GTK_SELECTION_NONE`.

The `.card` style class can be applied to any other widget to give it a similar
appearance.

If a widget with the `.card` style class also has the `.activatable` style
class, it will also have hover and active states similar to an activatable row
inside a boxed list.

If the `.card` style class is applied to a [class@Gtk.Button], it will get these
states automatically, without needing the `.activatable` class.

## Sidebars

The `.navigation-sidebar` style class can be applied to a [class@Gtk.ListBox]
or [class@Gtk.ListView] to make it look like a sidebar: it makes the
items rounded and padded and removes the default list background.

When using it on a flap child in an [class@Adw.Flap], the lack of background can
be problematic. In that case, it can be used together with the
[`.background`](#background) style class.

## App Icons

GNOME application icons require a shadow to be legible on a light background.
The `.icon-dropshadow` and `.lowres-icon` style classes provide it when used
with [class@Gtk.Image] or any other widget that contains an image.

`.lowres-icon` should be used for 32×32 or smaller icons, and `.icon-dropshadow`
should be used otherwise.

## Selection Mode Check Buttons

The `.selection-mode` style class can be added to a [class@Gtk.CheckButton] to
give it a larger and round appearance. These check buttons are intended to be
used for selecting items from a list or a grid.

## OSD

The `.osd` style class has a number of loosely related purposes depending on
what widget it's applied to.

Usually, it makes the widget background dark and partially transparent, and
makes its accent color white.

However, it has different effects in a few specific cases.

### Overlay Buttons

When used with [class@Gtk.Button], `.osd` can be used to create large standalone
buttons that overlap content, for example, the previous/next page arrows in an
image viewer. They appear dark and slightly larger than regular buttons.

It can be used in combination with [`.circular`](#circular) or [`.pill`](#pill).

### Floating Toolbars

When used along with the [`.toolbar`](#toolbars) style class, `.osd` gives the
box additional padding and round corners. This can be used to create floating
toolbars, such as video player controls.

### Progress Bars

When used with [class@Gtk.ProgressBar], `.osd` makes the progress bar thinner
and removes its visible trough.

OSD progress bars are intended to be used as [class@Gtk.Overlay] children,
attached to the top of the window.

## Background

The `.background` style class can be used with any widget to give it the default
[window](named-colors.html#window-colors) background and foreground colors.

This can be useful when a widget needs an opaque background - for example, a
flap child inside an [class@Adw.Flap].

It's equivalent to using the following CSS:

```css
.background {
  background-color: @window_bg_color;
  color: @window_fg_color;
}
```

## View

The `.view` style class can be used with any widget to give it the default
[view](named-colors.html#window-colors) background and foreground colors.

It's equivalent to using the following CSS:

```css
.view {
  background-color: @view_bg_color;
  color: @view_fg_color;
}
```

## Frame

The `.frame` style class can be used with any widget to give it the default
border.

It's equivalent to using the following CSS:

```css
.frame {
  border: 1px solid @borders;
}
```

## Flat Header Bar

The `.flat` style class can be used with a [class@Gtk.HeaderBar] to give it a
flat appearance.

## Compact Status Page

The `.compact` style class can be used with a [class@Adw.StatusPage] to make it
take less space. This is usually used with sidebars or popovers.

## Menu Popovers

The `.menu` style class can be used with a [class@Gtk.Popover] to give it a
menu-like appearance if it has a [class@Gtk.ListBox] or a [class@Gtk.ListView]
inside it.

## Development Window

The `.devel` style class can be used with [class@Gtk.Window]. This will give
any [class@Gtk.HeaderBar] inside that window a striped appearance.

This style class is typically used to indicate unstable or nightly applications.

## Inline Tab Bars & Search Bars

By default [class@Gtk.SearchBar] and [class@Adw.TabBar] look like a part of a
[class@Gtk.HeaderBar] and are intended to be used directly attached to one. With
the `.inline` style class they have neutral backgrounds and can be used in
different contexts instead.

# Deprecated Style Classes

The following style classes are deprecated and remain there for compatibility.
They shouldn't be used in new code.

## `.content`

The `.content` style class can be applied to a [class@Gtk.ListBox] to give it a
boxed list appearance. The [`.boxed-list`](#boxed-lists-cards) style class is
completely equivalent to it and should be used instead.

## `.sidebar`

The `.sidebar` style class adds a border at the end of the widget (`border-right`
for left-to-right text direction, `border-left` for right-to-left) and removes
background from any [class@Gtk.ListBox] or [class@Gtk.ListView] inside it.

It can be replaced by using the [`.navigation-sidebar`](#sidebars) style class
on the list widget, combined with a [class@Gtk.Separator] to achieve the border.

## `.app-notification`

The `.app-notification` style class is used with widgets like [class@Gtk.Box].
It adds [`.osd`](#osd) appearance to the widget and makes its bottom corners
round. When used together with a [class@Gtk.Overlay] and a [class@Gtk.Revealer],
it allows creating in-app notifications.

[class@Adw.ToastOverlay] can be used to replace it.