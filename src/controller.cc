/* $Id$
 *
 * Copyright (c) 2002  Daniel Elstner  <daniel.elstner@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License VERSION 2 as
 * published by the Free Software Foundation.  You are not allowed to
 * use any other version of the license; unless you got the explicit
 * permission from the author to do so.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "controller.h"

#include <libglademm.h>
#include <gtkmm/button.h>
#include <gtkmm/menu.h>

#include <config.h>


namespace Regexxer
{

/**** Regexxer::ControlItem ************************************************/

ControlItem::ControlItem(bool enable)
:
  enabled_       (enable),
  group_enabled_ (true)
{}

ControlItem::~ControlItem()
{}

void ControlItem::activate()
{
  if (enabled_ && group_enabled_)
    signal_activate_(); // emit
}

SigC::Slot0<void> ControlItem::slot()
{
  return SigC::slot(*this, &ControlItem::activate);
}

void ControlItem::connect(const SigC::Slot0<void>& slot_activated)
{
  signal_activate_.connect(slot_activated);
}

void ControlItem::add_widget(Gtk::Widget& widget)
{
  signal_set_sensitive_.connect(SigC::slot(widget, &Gtk::Widget::set_sensitive));
  widget.set_sensitive(enabled_ && group_enabled_);
}

void ControlItem::add_widgets(const Glib::RefPtr<Gnome::Glade::Xml>& xml,
                              const char* menuitem_name, const char* button_name)
{
  const SigC::Slot0<void> slot_activate = slot();

  Gtk::MenuItem* menuitem = 0;
  Gtk::Button*   button   = 0;

  if (menuitem_name && xml->get_widget(menuitem_name, menuitem))
  {
    menuitem->signal_activate().connect(slot_activate);
    add_widget(*menuitem);
  }

  if (button_name && xml->get_widget(button_name, button))
  {
    button->signal_clicked().connect(slot_activate);
    add_widget(*button);
  }
}

void ControlItem::set_enabled(bool enable)
{
  if (enable != enabled_)
  {
    enabled_ = enable;

    if (group_enabled_)
      signal_set_sensitive_(enabled_); // emit
  }
}

void ControlItem::set_group_enabled(bool enable)
{
  if (enable != group_enabled_)
  {
    group_enabled_ = enable;

    if (enabled_)
      signal_set_sensitive_(group_enabled_); // emit
  }
}

bool ControlItem::is_enabled() const
{
  return (enabled_ && group_enabled_);
}


/**** Regexxer::ControlGroup ***********************************************/

ControlGroup::ControlGroup(bool enable)
:
  enabled_ (enable)
{}

ControlGroup::~ControlGroup()
{}

void ControlGroup::add(ControlItem& control)
{
  signal_set_enabled_.connect(SigC::slot(control, &ControlItem::set_group_enabled));
  control.set_group_enabled(enabled_);
}

void ControlGroup::set_enabled(bool enable)
{
  if (enable != enabled_)
  {
    enabled_ = enable;
    signal_set_enabled_(enabled_); // emit
  }
}


/**** Regexxer::Controller *************************************************/

Controller::Controller()
:
  match_actions (true),
  save_file     (false),
  save_all      (false),
  undo          (false),
  preferences   (true),
  quit          (true),
  about         (true),
  find_files    (false),
  find_matches  (false),
  next_file     (false),
  prev_file     (false),
  next_match    (false),
  prev_match    (false),
  replace       (false),
  replace_file  (false),
  replace_all   (false)
{
  match_actions.add(undo);
  match_actions.add(find_files);
  match_actions.add(find_matches);
  match_actions.add(next_file);
  match_actions.add(prev_file);
  match_actions.add(next_match);
  match_actions.add(prev_match);
  match_actions.add(replace);
  match_actions.add(replace_file);
  match_actions.add(replace_all);
}

Controller::~Controller()
{}

void Controller::load_xml(const Glib::RefPtr<Gnome::Glade::Xml>& xml)
{
  save_file   .add_widgets(xml, "menuitem_save",         "button_save");
  save_all    .add_widgets(xml, "menuitem_save_all",     "button_save_all");
  undo        .add_widgets(xml, "menuitem_undo",         "button_undo");
  preferences .add_widgets(xml, "menuitem_preferences",  "button_preferences");
  quit        .add_widgets(xml, "menuitem_quit",         "button_quit");
  about       .add_widgets(xml, "menuitem_about",        0);
  next_file   .add_widgets(xml, "menuitem_next_file",    "button_next_file");
  prev_file   .add_widgets(xml, "menuitem_prev_file",    "button_prev_file");
  next_match  .add_widgets(xml, "menuitem_next_match",   "button_next_match");
  prev_match  .add_widgets(xml, "menuitem_prev_match",   "button_prev_match");
  replace     .add_widgets(xml, "menuitem_replace",      "button_replace");
  replace_file.add_widgets(xml, "menuitem_replace_file", "button_replace_file");
  replace_all .add_widgets(xml, "menuitem_replace_all",  "button_replace_all");
  find_files  .add_widgets(xml, 0,                       "button_find_files");
  find_matches.add_widgets(xml, 0,                       "button_find_matches");
}

} // namespace Regexxer

