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

#include "mainwindow.h"
#include "aboutdialog.h"
#include "filetree.h"
#include "globalstrings.h"
#include "imagebutton.h"
#include "pcreshell.h"
#include "prefdialog.h"
#include "statusline.h"
#include "stringutils.h"
#include "translation.h"

#include <glib.h>
#include <gconfmm.h>
#include <gtkmm.h>
#include <algorithm>
#include <functional>

#include <config.h>


namespace
{

const char *const builtin_filename_patterns[] =
{
  "*.[ch]",                                   // C
  "*.{c,cc,cpp,cxx,c++,C,h,hh,hpp,hxx,h++}",  // C/C++
  "*.{ccg,hg}",                               // gtkmmproc
  "*.idl",                                    // CORBA/COM
  "*.{java,jsp}",                             // Java/JSP
  "*.{pl,pm,cgi}",                            // Perl
  "*.py",                                     // Python
  "*.php3",                                   // PHP
  "*.{html,htm,shtml,js,wml}",                // HTML
  "*.{xml,xsl,css,dtd,xsd}",                  // XML/XSLT
  0
};

enum { BUSY_GUI_UPDATE_INTERVAL = 16 };

typedef Glib::RefPtr<Regexxer::FileBuffer> FileBufferPtr;


class FileErrorDialog : public Gtk::MessageDialog
{
public:
  FileErrorDialog(Gtk::Window& parent, const Glib::ustring& message,
                  Gtk::MessageType type, const Regexxer::FileTree::Error& error);
  virtual ~FileErrorDialog();
};

FileErrorDialog::FileErrorDialog(Gtk::Window& parent, const Glib::ustring& message,
                                 Gtk::MessageType type, const Regexxer::FileTree::Error& error)
:
  Gtk::MessageDialog(parent, message, type, Gtk::BUTTONS_OK, true)
{
  using namespace Gtk;

  const Glib::RefPtr<TextBuffer> buffer = TextBuffer::create();
  TextBuffer::iterator buffer_end = buffer->end();

  typedef std::list<Glib::ustring> ErrorList;
  const ErrorList& error_list = error.get_error_list();

  for (ErrorList::const_iterator perr = error_list.begin(); perr != error_list.end(); ++perr)
    buffer_end = buffer->insert(buffer_end, *perr + '\n');

  Box& box = *get_vbox();
  Frame *const frame = new Frame();
  box.pack_start(*manage(frame), PACK_EXPAND_WIDGET);
  frame->set_border_width(6); // HIG spacing
  frame->set_shadow_type(SHADOW_IN);

  ScrolledWindow *const scrollwin = new ScrolledWindow();
  frame->add(*manage(scrollwin));
  scrollwin->set_policy(POLICY_AUTOMATIC, POLICY_AUTOMATIC);

  TextView *const textview = new TextView(buffer);
  scrollwin->add(*manage(textview));
  textview->set_editable(false);
  textview->set_cursor_visible(false);
  textview->set_wrap_mode(WRAP_WORD);
  textview->set_pixels_below_lines(8);

  set_default_size(400, 250);
  frame->show_all();
}

FileErrorDialog::~FileErrorDialog()
{}

} // anonymous namespace


namespace Regexxer
{

/**** Regexxer::MainWindow::BusyAction *************************************/

class MainWindow::BusyAction
{
private:
  MainWindow& object_;

  BusyAction(const BusyAction&);
  BusyAction& operator=(const BusyAction&);

public:
  explicit BusyAction(MainWindow& object)
    : object_ (object) { object_.busy_action_enter(); }

  ~BusyAction() { object_.busy_action_leave(); }
};


/**** Regexxer::MainWindow *************************************************/

MainWindow::MainWindow()
:
  toolbar_                (0),
  entry_folder_           (0),
  entry_pattern_          (0),
  button_recursive_       (0),
  button_hidden_          (0),
  entry_regex_            (0),
  entry_substitution_     (0),
  button_multiple_        (0),
  button_caseless_        (0),
  filetree_               (0),
  textview_               (0),
  entry_preview_          (0),
  statusline_             (0),
  busy_action_running_    (false),
  busy_action_cancel_     (false),
  busy_action_iteration_  (0),
  undo_stack_             (new UndoStack())
{
  using SigC::bind;
  using SigC::slot;

  set_title_filename();
  set_default_size(640, 450);

  add(*Gtk::manage(create_main_vbox()));

  controller_.save_file   .connect(slot(*this, &MainWindow::on_save_file));
  controller_.save_all    .connect(slot(*this, &MainWindow::on_save_all));
  controller_.undo        .connect(slot(*this, &MainWindow::on_undo));
  controller_.preferences .connect(slot(*this, &MainWindow::on_preferences));
  controller_.quit        .connect(slot(*this, &MainWindow::on_quit));
  controller_.about       .connect(slot(*this, &MainWindow::on_about));
  controller_.find_files  .connect(slot(*this, &MainWindow::on_find_files));
  controller_.find_matches.connect(slot(*this, &MainWindow::on_exec_search));
  controller_.next_file   .connect(bind(slot(*this, &MainWindow::on_go_next_file), true));
  controller_.prev_file   .connect(bind(slot(*this, &MainWindow::on_go_next_file), false));
  controller_.next_match  .connect(bind(slot(*this, &MainWindow::on_go_next), true));
  controller_.prev_match  .connect(bind(slot(*this, &MainWindow::on_go_next), false));
  controller_.replace     .connect(slot(*this, &MainWindow::on_replace));
  controller_.replace_file.connect(slot(*this, &MainWindow::on_replace_file));
  controller_.replace_all .connect(slot(*this, &MainWindow::on_replace_all));

  show_all_children();

  Gnome::Conf::Client::get_default_client()
      ->signal_value_changed().connect(slot(*this, &MainWindow::on_conf_value_changed));

  entry_folder_->set_text(Util::shorten_pathname(
      Util::filename_to_utf8_fallback(Glib::get_current_dir())));
  entry_pattern_->set_text("*");
  button_recursive_->set_active(true);

  statusline_->signal_cancel_clicked.connect(slot(*this, &MainWindow::on_busy_action_cancel));

  filetree_->signal_switch_buffer.connect(
      slot(*this, &MainWindow::on_filetree_switch_buffer));

  filetree_->signal_bound_state_changed.connect(
      slot(*this, &MainWindow::on_bound_state_changed));

  filetree_->signal_file_count_changed.connect(
      slot(*this, &MainWindow::on_filetree_file_count_changed));

  filetree_->signal_match_count_changed.connect(
      slot(*this, &MainWindow::on_filetree_match_count_changed));

  filetree_->signal_modified_count_changed.connect(
      slot(*this, &MainWindow::on_filetree_modified_count_changed));

  filetree_->signal_pulse.connect(
      slot(*this, &MainWindow::on_busy_action_pulse));

  filetree_->signal_undo_stack_push.connect(
      slot(*this, &MainWindow::on_undo_stack_push));
}

MainWindow::~MainWindow()
{}

/**** Regexxer::MainWindow -- protected ************************************/

void MainWindow::on_hide()
{
  on_busy_action_cancel();

  // Kill the dialogs if they're mapped right now.  This isn't strictly
  // necessary since they'd be deleted in the destructor anyway.  But if we
  // have to do a lot of cleanup the dialogs would stay open for that time,
  // which doesn't look neat.

  {
    // Play safe and transfer ownership, and let the dtor do the delete.
    const std::auto_ptr<AboutDialog> temp (about_dialog_);
  }
  {
    const std::auto_ptr<PrefDialog> temp (pref_dialog_);
  }

  Gtk::Window::on_hide();
}

void MainWindow::on_style_changed(const Glib::RefPtr<Gtk::Style>& previous_style)
{
  Gtk::Window::on_style_changed(previous_style);

  FileBuffer::pango_context_changed(get_pango_context());
}

bool MainWindow::on_delete_event(GdkEventAny*)
{
  return !confirm_quit_request();
}

/**** Regexxer::MainWindow -- private **************************************/

Gtk::Widget* MainWindow::create_main_vbox()
{
  using namespace Gtk;

  std::auto_ptr<Box> vbox_main (new VBox());

  HandleBox *const menubar_handle = new HandleBox();
  vbox_main->pack_start(*manage(menubar_handle), PACK_SHRINK);

  MenuBar *const menubar = controller_.create_menubar();
  menubar_handle->add(*manage(menubar));

  HandleBox *const toolbar_handle = new HandleBox();
  vbox_main->pack_start(*manage(toolbar_handle), PACK_SHRINK);

  toolbar_ = controller_.create_toolbar();
  toolbar_handle->add(*manage(toolbar_));

  Box *const vbox_interior = new VBox();

  vbox_main->pack_start(*manage(vbox_interior), PACK_EXPAND_WIDGET);
  // HIG spacing.  This plus the window border width makes 12,
  // or plus the other border makes 12 between widgets.
  vbox_interior->set_border_width(6);

  statusline_ = new StatusLine();
  vbox_main->pack_start(*manage(statusline_), PACK_SHRINK);

  Paned *const paned = new HPaned();
  vbox_interior->pack_start(*manage(paned), PACK_EXPAND_WIDGET);

  paned->pack1(*manage(create_left_pane()),  EXPAND);
  paned->pack2(*manage(create_right_pane()), EXPAND);

  vbox_interior->pack_start(*manage(controller_.create_action_area()), PACK_SHRINK);

  return vbox_main.release();
}

Gtk::Widget* MainWindow::create_left_pane()
{
  using namespace Gtk;

  std::auto_ptr<Box> vbox (new VBox(false, 3));
  vbox->set_border_width(6); // HIG spacing

  Table *const table = new Table(3, 2, false);
  vbox->pack_start(*manage(table), PACK_SHRINK);
  table->set_spacings(6); // HIG

  Button *const button_folder = new ImageLabelButton(Stock::OPEN, _("Fol_der:"), true);
  table->attach(*manage(button_folder), 0, 1, 0, 1, FILL, AttachOptions(0));
  button_folder->signal_clicked().connect(SigC::slot(*this, &MainWindow::on_select_folder));

  Label *const label_pattern = new Label(_("Pattern:"), 0.0, 0.5);
  table->attach(*manage(label_pattern), 0, 1, 1, 2, FILL, AttachOptions(0));

  entry_folder_ = new Entry();
  table->attach(*manage(entry_folder_), 1, 2, 0, 1, EXPAND|FILL, AttachOptions(0));

  Combo *const combo_pattern = new Combo();
  table->attach(*manage(combo_pattern), 1, 2, 1, 2, EXPAND|FILL, AttachOptions(0));
  combo_pattern->set_popdown_strings(builtin_filename_patterns);

  entry_pattern_ = combo_pattern->get_entry();
  label_pattern->set_mnemonic_widget(*entry_pattern_);

  entry_folder_ ->signal_activate().connect(controller_.find_files.slot());
  entry_pattern_->signal_activate().connect(controller_.find_files.slot());
  entry_pattern_->signal_changed ().connect(SigC::slot(*this, &MainWindow::on_entry_pattern_changed));

  Box *const hbox = new HBox(false, 6 /* HIG */);
  table->attach(*manage(hbox), 0, 2, 2, 3, EXPAND|FILL, AttachOptions(0));

  button_recursive_ = new CheckButton(_("recursive"));
  hbox->pack_start(*manage(button_recursive_), PACK_SHRINK);

  button_hidden_ = new CheckButton(_("hidden"));
  hbox->pack_start(*manage(button_hidden_), PACK_SHRINK);

  Button *const button_find_files = new Button(Stock::FIND);
  hbox->pack_end(*manage(button_find_files), PACK_SHRINK);

  button_find_files->signal_clicked().connect(controller_.find_files.slot());
  controller_.find_files.add_widget(*button_find_files);

  Frame *const frame = new Frame();
  vbox->pack_start(*manage(frame), PACK_EXPAND_WIDGET);

  ScrolledWindow *const scrollwin = new ScrolledWindow();
  frame->add(*manage(scrollwin));

  filetree_ = new FileTree();
  scrollwin->add(*manage(filetree_));
  scrollwin->set_policy(POLICY_AUTOMATIC, POLICY_ALWAYS);

  entry_folder_->get_accessible()->set_name("Folder");

  tooltips_.set_tip(*entry_folder_,     _("The directory to be searched"));
  tooltips_.set_tip(*entry_pattern_,    _("A filename pattern as used by the shell. "
                                          "Character classes [ab] and csh style "
                                          "brace expressions {a,b} are supported."));
  tooltips_.set_tip(*button_recursive_, _("Recurse into subdirectories"));
  tooltips_.set_tip(*button_hidden_,    _("Also find hidden files"));
  tooltips_.set_tip(*button_find_files, _("Find all files that match the filename pattern"));

  return vbox.release();
}

Gtk::Widget* MainWindow::create_right_pane()
{
  using namespace Gtk;

  std::auto_ptr<Box> vbox (new VBox(false, 3));
  vbox->set_border_width(6); // HIG spacing

  Table *const table = new Table(2, 3, false);
  vbox->pack_start(*manage(table), PACK_SHRINK);
  table->set_spacings(6); // HIG

  Label *const label_search = new Label(_("Search:"), 0.0, 0.5);
  table->attach(*manage(label_search), 0, 1, 0, 1, FILL, AttachOptions(0));
  table->attach(*manage(entry_regex_ = new Entry()),  1, 2, 0, 1, EXPAND|FILL, AttachOptions(0));
  label_search->set_mnemonic_widget(*entry_regex_);

  Label *const label_replace = new Label(_("Replace:"), 0.0, 0.5);
  table->attach(*manage(label_replace), 0, 1, 1, 2, FILL, AttachOptions(0));
  table->attach(*manage(entry_substitution_ = new Entry()),  1, 2, 1, 2, EXPAND|FILL, AttachOptions(0));
  label_replace->set_mnemonic_widget(*entry_substitution_);

  entry_regex_       ->signal_activate().connect(controller_.find_matches.slot());
  entry_substitution_->signal_activate().connect(controller_.find_matches.slot());
  entry_substitution_->signal_changed ().connect(SigC::slot(*this, &MainWindow::update_preview));

  Box *const hbox_options = new HBox(false, 6 /* HIG */);
  table->attach(*manage(hbox_options), 2, 3, 0, 1, FILL, AttachOptions(0));
  // Prefix with U+202D LEFT-TO-RIGHT OVERRIDE so we won't end up with "g/" instead.
  hbox_options->pack_start(*manage(button_multiple_ = new CheckButton("\342\200\255/g")), PACK_SHRINK);
  hbox_options->pack_start(*manage(button_caseless_ = new CheckButton("\342\200\255/i")), PACK_SHRINK);

  Button *const button_find_matches = new Button(Stock::FIND);
  table->attach(*manage(button_find_matches), 2, 3, 1, 2, FILL, AttachOptions(0));

  controller_.find_matches.add_widget(*button_find_matches);
  button_find_matches->signal_clicked().connect(controller_.find_matches.slot());

  Frame *const frame = new Frame();
  vbox->pack_start(*manage(frame), PACK_EXPAND_WIDGET);

  Box *const vbox_textview = new VBox(false, 3);
  frame->add(*manage(vbox_textview));

  ScrolledWindow *const scrollwin = new ScrolledWindow();
  vbox_textview->pack_start(*manage(scrollwin), PACK_EXPAND_WIDGET);

  textview_ = new TextView(FileBuffer::create());
  scrollwin->add(*manage(textview_));
  textview_->set_editable(false);
  textview_->set_cursor_visible(false);

  entry_preview_ = new Entry();
  vbox_textview->pack_start(*manage(entry_preview_), PACK_SHRINK);
  entry_preview_->set_has_frame(false);
  entry_preview_->set_editable(false);
  entry_preview_->unset_flags(CAN_FOCUS);

  entry_preview_->get_accessible()->set_name(_("Preview"));

  tooltips_.set_tip(*entry_regex_,        _("A regular expression in Perl syntax"));
  tooltips_.set_tip(*entry_substitution_, _("The new string to substitute. As in Perl, you can "
                                            "refer to parts of the match using $1, $2, etc. "
                                            "or even $+, $&, $` and $'. The operators "
                                            "\\l, \\u, \\L, \\U and \\E are supported as well."));
  tooltips_.set_tip(*button_multiple_,    _("Find all possible matches in a line"));
  tooltips_.set_tip(*button_caseless_,    _("Do case insensitive matching"));
  tooltips_.set_tip(*button_find_matches, _("Find all matches of the regular expression"));
  tooltips_.set_tip(*entry_preview_,      _("Preview of the substitution"));

  return vbox.release();
}

void MainWindow::on_quit()
{
  if (confirm_quit_request())
    hide();
}

bool MainWindow::confirm_quit_request()
{
  if (filetree_->get_modified_count() == 0)
    return true;

  const Glib::ustring message = _("Some files haven't been saved yet.\nQuit anyway?");
  Gtk::MessageDialog dialog (*this, message, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_NONE, true);

  dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
  dialog.add_button(Gtk::Stock::QUIT,   Gtk::RESPONSE_OK);

  return (dialog.run() == Gtk::RESPONSE_OK);
}

void MainWindow::on_select_folder()
{
  using namespace Glib;

  Gtk::FileSelection filesel (_("Select a folder"));

  filesel.set_modal(true);
  filesel.set_transient_for(*this);
  filesel.hide_fileop_buttons();
  filesel.set_has_separator(false);
  filesel.get_file_list()->get_parent()->hide();
  filesel.set_default_size(400, -1);

  {
    std::string filename = filename_from_utf8(Util::expand_pathname(entry_folder_->get_text()));

    if (!filename.empty() && *filename.rbegin() != G_DIR_SEPARATOR)
      filename += G_DIR_SEPARATOR;

    filesel.set_filename(filename);
  }

  if (filesel.run() == Gtk::RESPONSE_OK)
  {
    std::string filename = filesel.get_filename();

    if (!filename.empty() && *filename.rbegin() != G_DIR_SEPARATOR)
    {
      // The new GTK+ file selector doesn't append '/' to directories anymore.
      if (file_test(filename, FILE_TEST_IS_DIR))
        filename += G_DIR_SEPARATOR;
      else
        entry_pattern_->set_text(filename_to_utf8(path_get_basename(filename)));
    }

    entry_folder_->set_text(Util::shorten_pathname(filename_to_utf8(path_get_dirname(filename))));
  }
}

void MainWindow::on_find_files()
{
  if (filetree_->get_modified_count() > 0)
  {
    const Glib::ustring message = _("Some files haven't been saved yet.\nContinue anyway?");
    Gtk::MessageDialog dialog (*this, message, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);

    if (dialog.run() != Gtk::RESPONSE_OK)
      return;
  }

  undo_stack_clear();

  std::string folder = Glib::filename_from_utf8(Util::expand_pathname(entry_folder_->get_text()));

  if (folder.empty())
    folder = Glib::get_current_dir();

  BusyAction busy (*this);

  try
  {
    Pcre::Pattern pattern (Util::shell_pattern_to_regex(entry_pattern_->get_text()), Pcre::DOTALL);

    filetree_->find_files(
        folder, pattern,
        button_recursive_->get_active(),
        button_hidden_->get_active());
  }
  catch (const Pcre::Error& error)
  {
    const Glib::ustring message = _("The file search pattern is invalid.");
    Gtk::MessageDialog dialog (*this, message, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    dialog.run();
  }
  catch (const FileTree::Error& error)
  {
    const Glib::ustring message = _("The following errors occurred during search:");
    FileErrorDialog dialog (*this, message, Gtk::MESSAGE_WARNING, error);
    dialog.run();
  }

  statusline_->set_file_count(filetree_->get_file_count());
}

void MainWindow::on_exec_search()
{
  BusyAction busy (*this);

  const Glib::ustring regex = entry_regex_->get_text();
  const bool caseless = button_caseless_->get_active();
  const bool multiple = button_multiple_->get_active();

  try
  {
    Pcre::Pattern pattern (regex, (caseless) ? Pcre::CASELESS : Pcre::CompileOptions(0));
    filetree_->find_matches(pattern, multiple);
  }
  catch (const Pcre::Error& error)
  {
    const int offset = error.offset();
    const Glib::ustring message = (offset >= 0 && unsigned(offset) < regex.length())
      ? Util::compose(_("Error in regular expression at `%1' (index %2):\n%3"),
                      regex.substr(offset, 1), Util::int_to_string(offset + 1), error.what())
      : Util::compose(_("Error in regular expression:\n%1"), error.what());

    Gtk::MessageDialog dialog (*this, message, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    dialog.run();

    if (offset >= 0 && offset < entry_regex_->get_text_length())
    {
      entry_regex_->grab_focus();
      entry_regex_->select_region(offset, offset + 1);
    }

    return;
  }

  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    statusline_->set_match_count(buffer->get_original_match_count());
    statusline_->set_match_index(buffer->get_match_index());
  }

  if (filetree_->get_match_count() > 0)
  {
    // Scrolling has to be post-poned after the redraw, otherwise we might
    // not end up where we want to.  So do that by installing an idle handler.

    Glib::signal_idle().connect(
        SigC::slot(*this, &MainWindow::after_exec_search),
        Glib::PRIORITY_HIGH_IDLE + 25); // slightly less than redraw (+20)
  }
}

bool MainWindow::after_exec_search()
{
  filetree_->select_first_file();
  on_go_next(true);

  return false;
}

void MainWindow::on_filetree_switch_buffer(FileInfoPtr fileinfo, int file_index)
{
  const FileBufferPtr old_buffer = FileBufferPtr::cast_static(textview_->get_buffer());

  if (fileinfo && fileinfo->buffer == old_buffer)
    return;

  if (old_buffer)
  {
    std::for_each(buffer_connections_.begin(), buffer_connections_.end(),
                  std::mem_fun_ref(&SigC::Connection::disconnect));

    buffer_connections_.clear();
    old_buffer->forget_current_match();
  }

  if (fileinfo)
  {
    const FileBufferPtr buffer = fileinfo->buffer;
    g_return_if_fail(buffer);

    textview_->set_buffer(buffer);
    textview_->set_editable(!fileinfo->load_failed);
    textview_->set_cursor_visible(!fileinfo->load_failed);

    if (!fileinfo->load_failed)
    {
      buffer_connections_.push_back(buffer->signal_modified_changed().
          connect(SigC::slot(*this, &MainWindow::on_buffer_modified_changed)));

      buffer_connections_.push_back(buffer->signal_bound_state_changed.
          connect(SigC::slot(*this, &MainWindow::on_bound_state_changed)));

      buffer_connections_.push_back(buffer->signal_preview_line_changed.
          connect(SigC::slot(*this, &MainWindow::update_preview)));
    }

    set_title_filename(Util::filename_to_utf8_fallback(fileinfo->fullname));

    controller_.replace_file.set_enabled(buffer->get_match_count() > 0);
    controller_.save_file.set_enabled(buffer->get_modified());

    statusline_->set_match_count(buffer->get_original_match_count());
    statusline_->set_match_index(buffer->get_match_index());
    statusline_->set_file_encoding(fileinfo->encoding);
  }
  else
  {
    textview_->set_buffer(FileBuffer::create());
    textview_->set_editable(false);
    textview_->set_cursor_visible(false);

    set_title_filename();

    controller_.replace_file.set_enabled(false);
    controller_.save_file.set_enabled(false);

    statusline_->set_match_count(0);
    statusline_->set_match_index(0);
    statusline_->set_file_encoding("");
  }

  statusline_->set_file_index(file_index);
  update_preview();
}

void MainWindow::on_bound_state_changed()
{
  BoundState bound = filetree_->get_bound_state();

  controller_.prev_file.set_enabled((bound & BOUND_FIRST) == 0);
  controller_.next_file.set_enabled((bound & BOUND_LAST)  == 0);

  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
    bound &= buffer->get_bound_state();

  controller_.prev_match.set_enabled((bound & BOUND_FIRST) == 0);
  controller_.next_match.set_enabled((bound & BOUND_LAST)  == 0);
}

void MainWindow::on_filetree_file_count_changed()
{
  const int file_count = filetree_->get_file_count();

  statusline_->set_file_count(file_count);
  controller_.find_matches.set_enabled(file_count > 0);
}

void MainWindow::on_filetree_match_count_changed()
{
  controller_.replace_all.set_enabled(filetree_->get_match_count() > 0);

  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
    controller_.replace_file.set_enabled(buffer->get_match_count() > 0);
}

void MainWindow::on_filetree_modified_count_changed()
{
  controller_.save_all.set_enabled(filetree_->get_modified_count() > 0);
}

void MainWindow::on_buffer_modified_changed()
{
  controller_.save_file.set_enabled(textview_->get_buffer()->get_modified());
}

void MainWindow::on_go_next_file(bool move_forward)
{
  filetree_->select_next_file(move_forward);
  on_go_next(move_forward);
}

void MainWindow::on_go_next(bool move_forward)
{
  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    if (const Glib::RefPtr<Gtk::TextMark> mark = buffer->get_next_match(move_forward))
    {
      textview_->scroll_to_mark(mark, 0.125);
      statusline_->set_match_index(buffer->get_match_index());
      return;
    }
  }

  if (filetree_->select_next_file(move_forward))
  {
    on_go_next(move_forward); // recursive call
  }
}

void MainWindow::on_replace()
{
  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    buffer->replace_current_match(entry_substitution_->get_text());
    on_go_next(true);
  }
}

void MainWindow::on_replace_file()
{
  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    buffer->replace_all_matches(entry_substitution_->get_text());
    statusline_->set_match_index(0);
  }
}

void MainWindow::on_replace_all()
{
  BusyAction busy (*this);

  filetree_->replace_all_matches(entry_substitution_->get_text());
  statusline_->set_match_index(0);
}

void MainWindow::on_save_file()
{
  try
  {
    filetree_->save_current_file();
  }
  catch (const FileTree::Error& error)
  {
    const std::list<Glib::ustring>& error_list = error.get_error_list();
    g_assert(error_list.size() == 1);

    Gtk::MessageDialog dialog (*this, error_list.front(), Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    dialog.run();
  }
}

void MainWindow::on_save_all()
{
  try
  {
    filetree_->save_all_files();
  }
  catch (const FileTree::Error& error)
  {
    const Glib::ustring message = _("The following errors occurred during save:");
    FileErrorDialog dialog (*this, message, Gtk::MESSAGE_ERROR, error);
    dialog.run();
  }
}

void MainWindow::on_undo_stack_push(UndoActionPtr action)
{
  undo_stack_->push(action);
  controller_.undo.set_enabled(true);
}

void MainWindow::on_undo()
{
  BusyAction busy (*this);

  undo_stack_->undo_step(SigC::slot(*this, &MainWindow::on_busy_action_pulse));
  controller_.undo.set_enabled(!undo_stack_->empty());
}

void MainWindow::undo_stack_clear()
{
  controller_.undo.set_enabled(false);
  undo_stack_.reset(new UndoStack());
}

void MainWindow::on_entry_pattern_changed()
{
  controller_.find_files.set_enabled(entry_pattern_->get_text_length() > 0);
}

void MainWindow::update_preview()
{
  if (const FileBufferPtr buffer = FileBufferPtr::cast_static(textview_->get_buffer()))
  {
    Glib::ustring preview;
    const int pos = buffer->get_line_preview(entry_substitution_->get_text(), preview);
    entry_preview_->set_text(preview);

    controller_.replace.set_enabled(pos >= 0);

    // Beware, strange code ahead!
    //
    // The goal is to scroll the preview entry so that it shows the entire
    // replaced text if possible.  In order to do that we first move the cursor
    // to 0, forcing scrolling to the left boundary.  Then we set the cursor to
    // the end of the replaced text, thus forcing the entry widget to scroll
    // again.  The replacement should then be entirely visible provided that it
    // fits into the entry.
    //
    // The problem is that Gtk::Entry doesn't update its scroll position
    // immediately but in an idle handler, thus any calls to set_position()
    // but the last one have no effect at all.
    //
    // To workaround that, we install an idle handler that's executed just
    // after the entry updated its scroll position, but before redrawing is
    // done.

    entry_preview_->set_position(0);

    if (pos > 0)
    {
      using namespace SigC;

      Glib::signal_idle().connect(
          bind_return(bind(slot(*entry_preview_, &Gtk::Editable::set_position), pos), false),
          Glib::PRIORITY_HIGH_IDLE + 17); // between scroll update (+ 15) and redraw (+ 20)
    }
  }
}

void MainWindow::set_title_filename(const Glib::ustring& filename)
{
  Glib::ustring title;

  if (!filename.empty())
  {
    title  = Glib::path_get_basename(filename);
    title += " (";
    title += Util::shorten_pathname(Glib::path_get_dirname(filename));
    title += ") \342\200\223 "; // U+2013 EN DASH
  }

  title += PACKAGE_NAME;

  set_title(title);
}

void MainWindow::busy_action_enter()
{
  g_return_if_fail(!busy_action_running_);

  controller_.match_actions.set_enabled(false);

  statusline_->pulse_start();

  busy_action_running_    = true;
  busy_action_cancel_     = false;
  busy_action_iteration_  = 0;
}

void MainWindow::busy_action_leave()
{
  g_return_if_fail(busy_action_running_);

  busy_action_running_ = false;
  busy_action_cancel_  = false;

  statusline_->pulse_stop();

  controller_.match_actions.set_enabled(true);
}

bool MainWindow::on_busy_action_pulse()
{
  g_return_val_if_fail(busy_action_running_, true);

  if (!busy_action_cancel_ && (++busy_action_iteration_ % BUSY_GUI_UPDATE_INTERVAL) == 0)
  {
    statusline_->pulse();

    const Glib::RefPtr<Glib::MainContext> context = Glib::MainContext::get_default();

    do {}
    while (context->iteration(false) && !busy_action_cancel_);
  }

  return busy_action_cancel_;
}

void MainWindow::on_busy_action_cancel()
{
  if (busy_action_running_)
    busy_action_cancel_ = true;
}

void MainWindow::on_about()
{
  if (about_dialog_.get())
  {
    about_dialog_->present();
  }
  else
  {
    std::auto_ptr<AboutDialog> dialog (new AboutDialog(*this));

    dialog->signal_hide().connect(SigC::slot(*this, &MainWindow::on_about_dialog_hide));
    dialog->show();

    about_dialog_ = dialog;
  }
}

void MainWindow::on_about_dialog_hide()
{
  // Play safe and transfer ownership, and let the dtor do the delete.
  const std::auto_ptr<AboutDialog> temp (about_dialog_);
}

void MainWindow::on_preferences()
{
  if (pref_dialog_.get())
  {
    pref_dialog_->get_dialog()->present();
  }
  else
  {
    std::auto_ptr<PrefDialog> dialog (new PrefDialog(*this));

    dialog->get_dialog()->signal_hide().connect(SigC::slot(*this, &MainWindow::on_pref_dialog_hide));
    dialog->get_dialog()->show();

    pref_dialog_ = dialog;
  }
}

void MainWindow::on_pref_dialog_hide()
{
  // Play safe and transfer ownership, and let the dtor do the delete.
  const std::auto_ptr<PrefDialog> temp (pref_dialog_);
}

void MainWindow::on_conf_value_changed(const Glib::ustring& key, const Gnome::Conf::Value& value)
{
  using namespace Gtk;

  REGEXXER_GCONFMM_VALUE_HACK(value);

  if (value.get_type() == Gnome::Conf::VALUE_STRING)
  {
    if (key.raw() == conf_key_textview_font)
    {
      const Pango::FontDescription font (value.get_string());
      textview_     ->modify_font(font);
      entry_preview_->modify_font(font);
    }
    else if (key.raw() == conf_key_toolbar_style)
    {
      toolbar_->set_toolbar_style(Util::enum_from_nick<ToolbarStyle>(value.get_string()));
    }
  }
  else if (value.get_type() == Gnome::Conf::VALUE_BOOL)
  {
    if (key.raw() == conf_key_override_direction)
    {
      const TextDirection direction = (value.get_bool()) ? TEXT_DIR_LTR : TEXT_DIR_NONE;
      entry_folder_      ->set_direction(direction);
      entry_pattern_     ->set_direction(direction);
      entry_regex_       ->set_direction(direction);
      entry_substitution_->set_direction(direction);
      filetree_          ->set_direction(direction);
      textview_          ->set_direction(direction);
      entry_preview_     ->set_direction(direction);
    }
  }
}

} // namespace Regexxer

