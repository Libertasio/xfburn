/* $Id$ */
/*
 * Copyright (c) 2005-2006 Jean-François Wauthy (pollux@xfce.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef	HAVE_CONFIG_H
#include <config.h>
#endif /* !HAVE_CONFIG_H */

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>

#ifdef HAVE_THUNAR_VFS
#include <thunar-vfs/thunar-vfs.h>
#endif

#include <exo/exo.h>

#include <libisofs.h>

#include "xfburn-data-composition.h"
#include "xfburn-global.h"

#include "xfburn-adding-progress.h"
#include "xfburn-composition.h"
#include "xfburn-burn-data-cd-composition-dialog.h"
#include "xfburn-burn-data-dvd-composition-dialog.h"
#include "xfburn-data-disc-usage.h"
#include "xfburn-main-window.h"
#include "xfburn-utils.h"

#define XFBURN_DATA_COMPOSITION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), XFBURN_TYPE_DATA_COMPOSITION, XfburnDataCompositionPrivate))

enum
{
  DATA_COMPOSITION_COLUMN_ICON,
  DATA_COMPOSITION_COLUMN_CONTENT,
  DATA_COMPOSITION_COLUMN_HUMANSIZE,
  DATA_COMPOSITION_COLUMN_SIZE,
  DATA_COMPOSITION_COLUMN_PATH,
  DATA_COMPOSITION_COLUMN_TYPE,
  DATA_COMPOSITION_N_COLUMNS
};

typedef enum
{
  DATA_COMPOSITION_TYPE_FILE,
  DATA_COMPOSITION_TYPE_DIRECTORY
} DataCompositionEntryType;


/* thread parameters */
typedef struct {
  char **filenames;
  int filec;
  XfburnDataComposition *dc;
} ThreadAddFilesCLIParams;

typedef struct {
  XfburnDataComposition *dc;
  GtkTreeModel *model;
  GtkTreeIter iter_where_insert;
  DataCompositionEntryType type;
} ThreadAddFilesActionParams;

typedef struct {
  XfburnDataComposition *composition;
  DataCompositionEntryType type;
  GtkWidget *widget;
  GtkTreeViewDropPosition position;
  GtkTreeIter iter_dummy;
} ThreadAddFilesDragParams;

/* prototypes */
static void xfburn_data_composition_class_init (XfburnDataCompositionClass *);
static void composition_interface_init (XfburnCompositionInterface *composition, gpointer iface_data);
static void xfburn_data_composition_init (XfburnDataComposition *dc);
static void xfburn_data_composition_finalize (GObject * object);

static void show_custom_controls (XfburnComposition *composition);
static void hide_custom_controls (XfburnComposition *composition);
static void load_from_file (XfburnComposition *composition, const gchar *file);
static void save_to_file (XfburnComposition *composition);

static gint directory_tree_sortfunc (GtkTreeModel * model, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data);

static void action_create_directory (GtkAction *, XfburnDataComposition *);
static void action_clear (GtkAction *, XfburnDataComposition *);
static void action_remove_selection (GtkAction *, XfburnDataComposition *);
static void action_rename_selection (GtkAction *, XfburnDataComposition *);
static void action_add_selected_files (GtkAction *, XfburnDataComposition *);

static gboolean cb_treeview_button_pressed (GtkTreeView * treeview, GdkEventButton * event, XfburnDataComposition * dc);
static void cb_treeview_row_activated (GtkTreeView * treeview, GtkTreePath * path, GtkTreeViewColumn * column, XfburnDataComposition * composition);
static void cb_selection_changed (GtkTreeSelection *selection, XfburnDataComposition * dc);
static void cb_begin_burn (XfburnDataDiscUsage * du, XfburnDataComposition * dc);
static void cb_cell_file_edited (GtkCellRenderer * renderer, gchar * path, gchar * newtext, XfburnDataComposition * dc);

static void cb_content_drag_data_rcv (GtkWidget * widget, GdkDragContext * dc, guint x, guint y, GtkSelectionData * sd,
                                      guint info, guint t, XfburnDataComposition * composition);
static void cb_content_drag_data_get (GtkWidget * widget, GdkDragContext * dc, GtkSelectionData * data, guint info,
                                      guint time, XfburnDataComposition * content);
static void cb_adding_done (XfburnAddingProgress *progress, XfburnDataComposition *dc);

/* thread entry points */
static void thread_add_files_cli (ThreadAddFilesCLIParams *params);
static void thread_add_files_action (ThreadAddFilesActionParams *params);
static void thread_add_files_drag (ThreadAddFilesDragParams *params);

/* thread helpers */
static gboolean thread_add_file_to_list_with_name (const gchar *name, XfburnDataComposition * dc, 
                                                   GtkTreeModel * model, const gchar * path, GtkTreeIter * iter, 
                                                   GtkTreeIter * insertion, GtkTreeViewDropPosition position);
static gboolean thread_add_file_to_list (XfburnDataComposition * dc, GtkTreeModel * model, const gchar * path, 
                                         GtkTreeIter * iter, GtkTreeIter * insertion, GtkTreeViewDropPosition position);
static IsoImage * generate_iso_image (XfburnDataComposition * dc);
                                  
typedef struct
{
  gchar *filename;
  gboolean modified;
 
  guint n_new_directory;

  GList *full_paths_to_add;
  gchar *selected_files;
  GtkTreePath *path_where_insert;

  GdkDragContext * dc;
  gboolean success;
  gboolean del;
  guint32 time;

  void *thread_params;
  
  GtkActionGroup *action_group;
  GtkUIManager *ui_manager;

  GtkWidget *toolbar;
  GtkWidget *entry_volume_name;
  GtkWidget *content;
  GtkWidget *disc_usage;
  GtkWidget *progress;

} XfburnDataCompositionPrivate;

/* globals */
static GtkHPanedClass *parent_class = NULL;
static guint instances = 0;

static const GtkActionEntry action_entries[] = {
  {"add-file", GTK_STOCK_ADD, N_("Add"), NULL, N_("Add the selected file(s) to the composition"),
   G_CALLBACK (action_add_selected_files),},
  {"create-dir", GTK_STOCK_NEW, N_("Create directory"), NULL, N_("Add a new directory to the composition"),
   G_CALLBACK (action_create_directory),},
  {"remove-file", GTK_STOCK_REMOVE, N_("Remove"), NULL, N_("Remove the selected file(s) from the composition"),
   G_CALLBACK (action_remove_selection),},
  {"clear", GTK_STOCK_CLEAR, N_("Clear"), NULL, N_("Clear the content of the composition"),
   G_CALLBACK (action_clear),},
  {"import-session", "xfburn-import-session", N_("Import"), NULL, N_("Import existing session"),},
  {"rename-file", GTK_STOCK_EDIT, N_("Rename"), NULL, N_("Rename the selected file"),
   G_CALLBACK (action_rename_selection),},
};

static const gchar *toolbar_actions[] = {
  "add-file",
  "remove-file",
  "create-dir",
  "clear",
  "import-session",
};

static GdkPixbuf *icon_directory = NULL, *icon_file = NULL;

/***************************/
/* XfburnDataComposition class */
/***************************/
GtkType
xfburn_data_composition_get_type (void)
{
  static GtkType data_composition_type = 0;

  if (!data_composition_type) {
    static const GTypeInfo data_composition_info = {
      sizeof (XfburnDataCompositionClass),
      NULL,
      NULL,
      (GClassInitFunc) xfburn_data_composition_class_init,
      NULL,
      NULL,
      sizeof (XfburnDataComposition),
      0,
      (GInstanceInitFunc) xfburn_data_composition_init
    };

    static const GInterfaceInfo composition_info = {
      (GInterfaceInitFunc) composition_interface_init,    /* interface_init */
      NULL,                                               /* interface_finalize */
      NULL                                                /* interface_data */
    };
    
    data_composition_type = g_type_register_static (GTK_TYPE_VBOX, "XfburnDataComposition", &data_composition_info, 0);
    
    g_type_add_interface_static (data_composition_type, XFBURN_TYPE_COMPOSITION, &composition_info);
  }

  return data_composition_type;
}

static void
xfburn_data_composition_class_init (XfburnDataCompositionClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (XfburnDataCompositionPrivate));
  
  parent_class = g_type_class_peek_parent (klass);
  
  object_class->finalize = xfburn_data_composition_finalize;
}

static void
composition_interface_init (XfburnCompositionInterface *composition, gpointer iface_data)
{
  composition->show_custom_controls = show_custom_controls;
  composition->hide_custom_controls = hide_custom_controls;
  composition->load = load_from_file;
  composition->save = save_to_file;
}

static void
xfburn_data_composition_init (XfburnDataComposition * composition)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (composition);
  
  gint x, y;
  ExoToolbarsModel *model_toolbar;
  gint toolbar_position;
  GtkWidget *hbox_toolbar;
  GtkWidget *hbox, *label;
  GtkWidget *scrolled_window;
  GtkTreeStore *model;
  GtkTreeViewColumn *column_file;
  GtkCellRenderer *cell_icon, *cell_file;
  GtkTreeSelection *selection;
  GtkAction *action = NULL;
  GdkScreen *screen;
  GtkIconTheme *icon_theme;
  
  const gchar ui_string[] = "<ui> <popup name=\"popup-menu\">"
    "<menuitem action=\"create-dir\"/>" "<separator/>"
    "<menuitem action=\"rename-file\"/>" "<menuitem action=\"remove-file\"/>" "</popup></ui>";

  GtkTargetEntry gte_src[] =  { { "XFBURN_TREE_PATHS", GTK_TARGET_SAME_WIDGET, DATA_COMPOSITION_DND_TARGET_INSIDE } };
  GtkTargetEntry gte_dest[] = { { "XFBURN_TREE_PATHS", GTK_TARGET_SAME_WIDGET, DATA_COMPOSITION_DND_TARGET_INSIDE },
                                { "text/plain", 0, DATA_COMPOSITION_DND_TARGET_TEXT_PLAIN },
                                { "text/uri-list", 0, DATA_COMPOSITION_DND_TARGET_TEXT_URI_LIST },
                              };

  priv->full_paths_to_add = NULL;

  instances++;
  
  /* initialize static members */
  screen = gtk_widget_get_screen (GTK_WIDGET (composition));
  icon_theme = gtk_icon_theme_get_for_screen (screen);

  gtk_icon_size_lookup (GTK_ICON_SIZE_SMALL_TOOLBAR, &x, &y);
  if (!icon_directory)
    icon_directory = gtk_icon_theme_load_icon (icon_theme, "gnome-fs-directory", x, 0, NULL);
  if (!icon_file)
    icon_file = gtk_icon_theme_load_icon (icon_theme, "gnome-fs-regular", x, 0, NULL);

  /* create ui manager */
  priv->action_group = gtk_action_group_new ("xfburn-data-composition");
  gtk_action_group_set_translation_domain (priv->action_group, GETTEXT_PACKAGE);
  gtk_action_group_add_actions (priv->action_group, action_entries, G_N_ELEMENTS (action_entries),
                                GTK_WIDGET (composition));

  priv->ui_manager = gtk_ui_manager_new ();
  gtk_ui_manager_insert_action_group (priv->ui_manager, priv->action_group, 0);

  gtk_ui_manager_add_ui_from_string (priv->ui_manager, ui_string, -1, NULL);

  hbox_toolbar = gtk_hbox_new (FALSE, 5);
  gtk_box_pack_start (GTK_BOX (composition), hbox_toolbar, FALSE, TRUE, 0);
  gtk_widget_show (hbox_toolbar);
  
  /* toolbar */
  model_toolbar = exo_toolbars_model_new ();
  exo_toolbars_model_set_actions (model_toolbar, (gchar **) toolbar_actions, G_N_ELEMENTS (toolbar_actions));
  toolbar_position = exo_toolbars_model_add_toolbar (model_toolbar, -1, "content-toolbar");
  exo_toolbars_model_set_style (model_toolbar, GTK_TOOLBAR_BOTH, toolbar_position);

  exo_toolbars_model_add_item (model_toolbar, toolbar_position, -1, "add-file", EXO_TOOLBARS_ITEM_TYPE);
  exo_toolbars_model_add_item (model_toolbar, toolbar_position, -1, "create-dir", EXO_TOOLBARS_ITEM_TYPE);
  exo_toolbars_model_add_separator (model_toolbar, toolbar_position, -1);
  exo_toolbars_model_add_item (model_toolbar, toolbar_position, -1, "remove-file", EXO_TOOLBARS_ITEM_TYPE);
  exo_toolbars_model_add_item (model_toolbar, toolbar_position, -1, "clear", EXO_TOOLBARS_ITEM_TYPE);
  //exo_toolbars_model_add_separator (model_toolbar, toolbar_position, -1);
  //exo_toolbars_model_add_item (model_toolbar, toolbar_position, -1, "import-session", EXO_TOOLBARS_ITEM_TYPE);

  priv->toolbar = exo_toolbars_view_new_with_model (priv->ui_manager, model_toolbar);
  gtk_box_pack_start (GTK_BOX (hbox_toolbar), priv->toolbar, TRUE, TRUE, 0);
  gtk_widget_show (priv->toolbar);

    
  /* volume name */
  hbox = gtk_hbox_new (FALSE, 5);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 10);
  gtk_box_pack_start (GTK_BOX (hbox_toolbar), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);
  
  label = gtk_label_new (_("Volume name :"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
  
  priv->entry_volume_name = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (priv->entry_volume_name), _(DATA_COMPOSITION_DEFAULT_NAME));
  gtk_box_pack_start (GTK_BOX (hbox), priv->entry_volume_name, FALSE, FALSE, 0);
  gtk_widget_show (priv->entry_volume_name);
  
  /* content treeview */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);
  gtk_widget_show (scrolled_window);
  gtk_box_pack_start (GTK_BOX (composition), scrolled_window, TRUE, TRUE, 0);

  priv->content = exo_tree_view_new ();
  model = gtk_tree_store_new (DATA_COMPOSITION_N_COLUMNS, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING,
                              G_TYPE_UINT64, G_TYPE_STRING, G_TYPE_UINT);
							  
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (model), DATA_COMPOSITION_COLUMN_CONTENT,
                                   directory_tree_sortfunc, NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model), DATA_COMPOSITION_COLUMN_CONTENT, GTK_SORT_ASCENDING);
  gtk_tree_view_set_model (GTK_TREE_VIEW (priv->content), GTK_TREE_MODEL (model));
  gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (priv->content), TRUE);
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->content));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
  gtk_widget_show (priv->content);
  gtk_container_add (GTK_CONTAINER (scrolled_window), priv->content);

  column_file = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column_file, _("Contents"));

  cell_icon = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column_file, cell_icon, FALSE);
  gtk_tree_view_column_set_attributes (column_file, cell_icon, "pixbuf", DATA_COMPOSITION_COLUMN_ICON, NULL);
  g_object_set (cell_icon, "xalign", 0.0, "ypad", 0, NULL);

  cell_file = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column_file, cell_file, TRUE);
  gtk_tree_view_column_set_attributes (column_file, cell_file, "text", DATA_COMPOSITION_COLUMN_CONTENT, NULL);
  g_signal_connect (G_OBJECT (cell_file), "edited", G_CALLBACK (cb_cell_file_edited), composition);
  g_object_set (G_OBJECT (cell_file), "editable", TRUE, NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (priv->content), column_file);

  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (priv->content), -1, _("Size"),
                                               gtk_cell_renderer_text_new (), "text", DATA_COMPOSITION_COLUMN_HUMANSIZE,
                                               NULL);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (priv->content), -1, _("Local Path"),
                                               gtk_cell_renderer_text_new (), "text", DATA_COMPOSITION_COLUMN_PATH, NULL);

  /* Contents column */
  gtk_tree_view_column_set_resizable (gtk_tree_view_get_column (GTK_TREE_VIEW (priv->content), 0), 1);
  gtk_tree_view_column_set_min_width (gtk_tree_view_get_column (GTK_TREE_VIEW (priv->content), 0), 200);
  /* Size (HUMANSIZE) column */
  gtk_tree_view_column_set_resizable (gtk_tree_view_get_column (GTK_TREE_VIEW (priv->content), 1), 1);
  gtk_tree_view_column_set_min_width (gtk_tree_view_get_column (GTK_TREE_VIEW (priv->content), 1), 60);
  /* Local Path (PATH) column */
  gtk_tree_view_column_set_resizable (gtk_tree_view_get_column (GTK_TREE_VIEW (priv->content), 2), 1);


  g_signal_connect (G_OBJECT (priv->content), "row-activated", G_CALLBACK (cb_treeview_row_activated), composition);
  g_signal_connect (G_OBJECT (priv->content), "button-press-event",
                    G_CALLBACK (cb_treeview_button_pressed), composition);
  g_signal_connect (G_OBJECT (selection), "changed", G_CALLBACK (cb_selection_changed), composition);
                    
  /* adding progress window */
  priv->progress = GTK_WIDGET (xfburn_adding_progress_new ()); 
  g_signal_connect (G_OBJECT (priv->progress), "adding-done", G_CALLBACK (cb_adding_done), composition);
  gtk_window_set_transient_for (GTK_WINDOW (priv->progress), GTK_WINDOW (xfburn_main_window_get_instance ()));
  /* FIXME: progress should have a busy cursor */

  /* disc usage */
  priv->disc_usage = xfburn_data_disc_usage_new ();
  gtk_box_pack_start (GTK_BOX (composition), priv->disc_usage, FALSE, FALSE, 5);
  gtk_widget_show (priv->disc_usage);
  g_signal_connect (G_OBJECT (priv->disc_usage), "begin-burn", G_CALLBACK (cb_begin_burn), composition);

  /* set up DnD */
  gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (priv->content), GDK_BUTTON1_MASK, gte_src,
                                          G_N_ELEMENTS (gte_src), GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (G_OBJECT (priv->content), "drag-data-get", G_CALLBACK (cb_content_drag_data_get),
                    composition);
  gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (priv->content), gte_dest, G_N_ELEMENTS (gte_dest),
                                        GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (G_OBJECT (priv->content), "drag-data-received", G_CALLBACK (cb_content_drag_data_rcv),
                    composition);
                    
  action = gtk_action_group_get_action (priv->action_group, "remove-file");
  gtk_action_set_sensitive (GTK_ACTION (action), FALSE);
}

static void
xfburn_data_composition_finalize (GObject * object)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (object);
  
  g_free (priv->filename);
  
  /* free static members */
  instances--;
  if (instances == 0) {
    if (icon_directory) {
      g_object_unref (icon_directory);
      icon_directory = NULL;
    }
    if (icon_file) {
      g_object_unref (icon_file);
      icon_file = NULL;
    }
  }
  
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*************/
/* internals */
/*************/
static void
show_custom_controls (XfburnComposition *composition)
{
  DBG ("show");
}

static void
hide_custom_controls (XfburnComposition *composition)
{
  DBG ("hide");
}

static void
cb_begin_burn (XfburnDataDiscUsage * du, XfburnDataComposition * dc)
{
  XfburnMainWindow *mainwin = xfburn_main_window_get_instance ();
  GtkWidget *dialog = NULL;
  IsoImage *image = NULL;

  if (!iso_init()) {
    g_critical ("Could not initialize libisofs!");
    return;
  }

  image = generate_iso_image (XFBURN_DATA_COMPOSITION (dc));
  
  switch (xfburn_data_disc_usage_get_disc_type (du)) {
  case CD_DISC:
    dialog = xfburn_burn_data_cd_composition_dialog_new (image);
    break;
  case DVD_DISC:
    dialog = xfburn_burn_data_dvd_composition_dialog_new (image);
    break;
  }

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (mainwin));
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

static void
cb_treeview_row_activated (GtkTreeView * treeview, GtkTreePath * path, GtkTreeViewColumn * column,
                         XfburnDataComposition * composition)
{
  gtk_tree_view_expand_row (treeview, path, FALSE);
}

static void
cb_selection_changed (GtkTreeSelection *selection, XfburnDataComposition * dc)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  gint n_selected_rows;
  GtkAction *action = NULL;
  
  
  n_selected_rows = gtk_tree_selection_count_selected_rows (selection);
  if (n_selected_rows == 0) {
    action = gtk_action_group_get_action (priv->action_group, "add-file");
    gtk_action_set_sensitive (GTK_ACTION (action), TRUE);
    action = gtk_action_group_get_action (priv->action_group, "create-dir");
    gtk_action_set_sensitive (GTK_ACTION (action), TRUE);  
    action = gtk_action_group_get_action (priv->action_group, "remove-file");
    gtk_action_set_sensitive (GTK_ACTION (action), FALSE);  
  } else if (n_selected_rows == 1) {
    action = gtk_action_group_get_action (priv->action_group, "add-file");
    gtk_action_set_sensitive (GTK_ACTION (action), TRUE);
    action = gtk_action_group_get_action (priv->action_group, "create-dir");
    gtk_action_set_sensitive (GTK_ACTION (action), TRUE);  
    action = gtk_action_group_get_action (priv->action_group, "remove-file");
    gtk_action_set_sensitive (GTK_ACTION (action), TRUE);
  } else {
    action = gtk_action_group_get_action (priv->action_group, "add-file");
    gtk_action_set_sensitive (GTK_ACTION (action), FALSE);
    action = gtk_action_group_get_action (priv->action_group, "create-dir");
    gtk_action_set_sensitive (GTK_ACTION (action), FALSE);  
    action = gtk_action_group_get_action (priv->action_group, "remove-file");
    gtk_action_set_sensitive (GTK_ACTION (action), TRUE);     
  }
}

static gboolean
cb_treeview_button_pressed (GtkTreeView * treeview, GdkEventButton * event, XfburnDataComposition * dc)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  
  if ((event->button == 1) && (event->type == GDK_BUTTON_PRESS)) {
    GtkTreePath *path;
    
    if (gtk_tree_view_get_path_at_pos (treeview, event->x, event->y, &path, NULL, NULL, NULL)) {
      gtk_tree_path_free (path);
    } else {
      GtkTreeSelection *selection;

      selection = gtk_tree_view_get_selection (treeview);
      gtk_tree_selection_unselect_all (selection);
    }
    
    return FALSE;
  }
  
  if ((event->button == 3) && (event->type == GDK_BUTTON_PRESS)) {
    GtkTreeSelection *selection;
    GtkTreePath *path;
    GtkWidget *menu_popup;
    GtkWidget *menuitem_remove;
    GtkWidget *menuitem_rename;

    selection = gtk_tree_view_get_selection (treeview);

    if (gtk_tree_view_get_path_at_pos (treeview, event->x, event->y, &path, NULL, NULL, NULL)) {
      gtk_tree_selection_unselect_all (selection);
      gtk_tree_selection_select_path (selection, path);
      gtk_tree_path_free (path);
    }

    menu_popup = gtk_ui_manager_get_widget (priv->ui_manager, "/popup-menu");
    menuitem_remove = gtk_ui_manager_get_widget (priv->ui_manager, "/popup-menu/remove-file");
    menuitem_rename = gtk_ui_manager_get_widget (priv->ui_manager, "/popup-menu/rename-file");

    if (gtk_tree_selection_count_selected_rows (selection) >= 1)
      gtk_widget_set_sensitive (menuitem_remove, TRUE);
    else
      gtk_widget_set_sensitive (menuitem_remove, FALSE);
    if (gtk_tree_selection_count_selected_rows (selection) == 1)
      gtk_widget_set_sensitive (menuitem_rename, TRUE);
    else
      gtk_widget_set_sensitive (menuitem_rename, FALSE);

    gtk_menu_popup (GTK_MENU (menu_popup), NULL, NULL, NULL, NULL, event->button, gtk_get_current_event_time ());
    return TRUE;
  }

  return FALSE;
}

static gint
directory_tree_sortfunc (GtkTreeModel * model, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data)
{
  /* adapted from gnomebaker */
  gchar *aname, *bname;
  DataCompositionEntryType atype = -1, btype = -1;
  gint result = 0;

  gtk_tree_model_get (model, a, DATA_COMPOSITION_COLUMN_CONTENT, &aname, DATA_COMPOSITION_COLUMN_TYPE, &atype, -1);
  gtk_tree_model_get (model, b, DATA_COMPOSITION_COLUMN_CONTENT, &bname, DATA_COMPOSITION_COLUMN_TYPE, &btype, -1);

  if ( (atype == DATA_COMPOSITION_TYPE_DIRECTORY) && (btype != DATA_COMPOSITION_TYPE_DIRECTORY) )
    result = -1;
  else if ( (atype != DATA_COMPOSITION_TYPE_DIRECTORY) && (btype == DATA_COMPOSITION_TYPE_DIRECTORY) )
    result = 1;
  else
    result = g_ascii_strcasecmp (aname, bname);

  g_free (aname);
  g_free (bname);

  return result;
}

static gboolean
file_exists_on_same_level (GtkTreeModel * model, GtkTreePath * path, gboolean skip_path, const gchar *filename)
{
  GtkTreePath *current_path = NULL;
  GtkTreeIter current_iter;
  
  current_path = gtk_tree_path_copy (path);
  for (;gtk_tree_path_prev (current_path););
   
  if (gtk_tree_model_get_iter (model, &current_iter, current_path)) {
    do {
      gchar *current_filename = NULL;
      
      if (skip_path && gtk_tree_path_compare (path, current_path) == 0) {
        gtk_tree_path_next (current_path);
        continue;
      }

      gtk_tree_model_get (model, &current_iter, DATA_COMPOSITION_COLUMN_CONTENT, &current_filename, -1);
      if (strcmp (current_filename, filename) == 0) {
        g_free (current_filename);
        gtk_tree_path_free (current_path);
        return TRUE;
      }
      
      g_free (current_filename);
      gtk_tree_path_next (current_path);
    } while (gtk_tree_model_iter_next (model, &current_iter));
  }
  
  gtk_tree_path_free (current_path);
  return FALSE;
}

static void
cb_cell_file_edited (GtkCellRenderer * renderer, gchar * path, gchar * newtext, XfburnDataComposition * dc)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreePath *real_path;

  if (strlen (newtext) == 0) {
    xfce_err (_("You must give a name to the file"));
    return;
  }
    
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
  real_path = gtk_tree_path_new_from_string (path);

  if (gtk_tree_model_get_iter (model, &iter, real_path)) {
    if (file_exists_on_same_level (model, real_path, TRUE, newtext)) {
      xfce_err (_("A file with the same name is already present in the composition"));
    }
    else {
      gtk_tree_store_set (GTK_TREE_STORE (model), &iter, DATA_COMPOSITION_COLUMN_CONTENT, newtext, -1);
    }
  }

  gtk_tree_path_free (real_path);
}

static void
cb_adding_done (XfburnAddingProgress *progress, XfburnDataComposition *dc)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);

  gtk_widget_hide (priv->progress);

  if (priv->selected_files) {
    g_free (priv->selected_files);
    priv->selected_files = NULL;
  }

  if (priv->path_where_insert) {
    gtk_tree_path_free (priv->path_where_insert);
    priv->path_where_insert = NULL;
  }

  if (priv->full_paths_to_add) {
    g_list_foreach (priv->full_paths_to_add, (GFunc) g_free, NULL);
    g_list_free (priv->full_paths_to_add);
    priv->full_paths_to_add = NULL;
  }

  g_free (priv->thread_params);
  xfburn_default_cursor (priv->content);
}

static void
action_rename_selection (GtkAction * action, XfburnDataComposition * dc)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GList *list;
  GtkTreePath *path;
  GtkTreeViewColumn *column;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->content));
  list = gtk_tree_selection_get_selected_rows (selection, &model);

  path = (GtkTreePath *) list->data;
  column = gtk_tree_view_get_column (GTK_TREE_VIEW (priv->content), DATA_COMPOSITION_COLUMN_CONTENT - 1);
  /* -1 because of COLUMN_ICON */
  
  gtk_tree_view_set_cursor (GTK_TREE_VIEW (priv->content), path, column, TRUE);

  gtk_tree_path_free (path);
  g_list_free (list);
}

static void
action_create_directory (GtkAction * action, XfburnDataComposition * dc)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  
  GtkTreeModel *model;
  GtkTreeSelection *selection;
  GList *selected_paths = NULL;
  GtkTreePath *path_where_insert = NULL;
  GtkTreeIter iter_where_insert, iter_directory;
  DataCompositionEntryType type = -1;
  gchar *humansize = NULL;
  
  GtkTreePath *inserted_path = NULL;
  gchar *directory_text = NULL;
  
  GtkTreeViewColumn *column;
  GtkTreePath *path = NULL;
  
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->content));
  selected_paths = gtk_tree_selection_get_selected_rows (selection, NULL);
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
    
  if (selected_paths) {
    path_where_insert = (GtkTreePath *) (selected_paths->data);

    gtk_tree_model_get_iter (model, &iter_where_insert, path_where_insert);
    gtk_tree_model_get (model, &iter_where_insert, DATA_COMPOSITION_COLUMN_TYPE, &type, -1);
  }
  
  if (type == DATA_COMPOSITION_TYPE_DIRECTORY) {
    gtk_tree_store_append (GTK_TREE_STORE (model), &iter_directory, &iter_where_insert);
    gtk_tree_view_expand_row (GTK_TREE_VIEW (priv->content), path_where_insert, FALSE);
  } else if (type == DATA_COMPOSITION_TYPE_FILE) {
    GtkTreeIter parent;
        
    if (gtk_tree_model_iter_parent (model, &parent, &iter_where_insert))
      gtk_tree_store_append (GTK_TREE_STORE (model), &iter_directory, &parent);
    else
      gtk_tree_store_append (GTK_TREE_STORE (model), &iter_directory, NULL);
  } else {
    gtk_tree_store_append (GTK_TREE_STORE (model), &iter_directory, NULL);
  }
  
  humansize = xfburn_humanreadable_filesize (4);
  
  inserted_path = gtk_tree_model_get_path (model, &iter_directory);
  if (file_exists_on_same_level (model, inserted_path, TRUE, _("New directory")))
    directory_text = g_strdup_printf ("%s %d", _("New directory"), ++(priv->n_new_directory));
  else
    directory_text = g_strdup (_("New directory"));
  gtk_tree_path_free (inserted_path);
  
  gtk_tree_store_set (GTK_TREE_STORE (model), &iter_directory,
                      DATA_COMPOSITION_COLUMN_ICON, icon_directory,                     
                      DATA_COMPOSITION_COLUMN_CONTENT, directory_text,
                      DATA_COMPOSITION_COLUMN_HUMANSIZE, humansize,
                      DATA_COMPOSITION_COLUMN_SIZE, (guint64) 4,
                      DATA_COMPOSITION_COLUMN_TYPE, DATA_COMPOSITION_TYPE_DIRECTORY, -1);
  g_free (directory_text);
  g_free (humansize);
  
  xfburn_data_disc_usage_add_size (XFBURN_DATA_DISC_USAGE (priv->disc_usage), 4);
  
  gtk_widget_realize (priv->content);
  
  /* put the cell renderer in edition mode */
  column = gtk_tree_view_get_column (GTK_TREE_VIEW (priv->content), DATA_COMPOSITION_COLUMN_CONTENT - 1);
  /* -1 because of COLUMN_ICON */
  path = gtk_tree_model_get_path (model, &iter_directory);
  
  gtk_tree_view_set_cursor (GTK_TREE_VIEW (priv->content), path, column, TRUE);
  gtk_tree_path_free (path);
}

static void
remove_row_reference (GtkTreeRowReference *reference, XfburnDataCompositionPrivate *priv)
{
  GtkTreePath *path = NULL;
  GtkTreeModel *model;
  
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
  
  path = gtk_tree_row_reference_get_path (reference);
  if (path) {
    GtkTreeIter iter;
    
    if (gtk_tree_model_get_iter (model, &iter, path)) {
      GtkTreeIter parent, iter_temp;
      guint64 size = 0;
      
      gtk_tree_model_get (model, &iter, DATA_COMPOSITION_COLUMN_SIZE, &size, -1);
      xfburn_data_disc_usage_sub_size (XFBURN_DATA_DISC_USAGE (priv->disc_usage), size);

      iter_temp = iter;
      while (gtk_tree_model_iter_parent (model, &parent, &iter_temp)) {
        guint64 old_size;
        gchar *humansize = NULL;

        /* updates parent directories size */
        gtk_tree_model_get (model, &parent, DATA_COMPOSITION_COLUMN_SIZE, &old_size, -1);

        humansize = xfburn_humanreadable_filesize (old_size - size);
        gtk_tree_store_set (GTK_TREE_STORE (model), &parent, 
                            DATA_COMPOSITION_COLUMN_HUMANSIZE, humansize,
                            DATA_COMPOSITION_COLUMN_SIZE, old_size - size, -1);

        iter_temp = parent;

        g_free (humansize);
      }
      
      gtk_tree_store_remove (GTK_TREE_STORE (model), &iter);
    }
    
    gtk_tree_path_free (path);
  }
  
  gtk_tree_row_reference_free (reference);
}

static void
action_remove_selection (GtkAction * action, XfburnDataComposition * dc)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GList *list_paths = NULL, *el;
  GList *references = NULL;
  
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->content));
  list_paths = gtk_tree_selection_get_selected_rows (selection, &model);

  el = list_paths;
  while (el) {
    GtkTreePath *path = NULL;
    GtkTreeRowReference *reference = NULL;
  
    path = (GtkTreePath *) el->data;
    reference = gtk_tree_row_reference_new (model, path);
    gtk_tree_path_free (path);
  
    if (reference)
      references = g_list_prepend (references, reference);
    
    el = g_list_next (el);
  } 
  g_list_free (list_paths);
  
  g_list_foreach (references, (GFunc) remove_row_reference, priv);
  g_list_free (references);
}


static void
action_add_selected_files (GtkAction *action, XfburnDataComposition *dc)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  XfburnFileBrowser *browser = xfburn_main_window_get_file_browser (xfburn_main_window_get_instance ());
  
  gchar *selected_files = NULL;
  
  xfburn_busy_cursor (priv->content);
  selected_files = xfburn_file_browser_get_selection (browser);
  
  if (selected_files) {
    GtkTreeSelection *selection;
    GList *selected_paths = NULL;
    ThreadAddFilesActionParams *params;

    xfburn_adding_progress_show (XFBURN_ADDING_PROGRESS (priv->progress));

    params = g_new (ThreadAddFilesActionParams, 1);
    params->dc = dc;
    params->type = -1;
    priv->path_where_insert = NULL;
    
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->content));
    selected_paths = gtk_tree_selection_get_selected_rows (selection, NULL);
    params->model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
    
    if (selected_paths) {
      priv->path_where_insert = (GtkTreePath *) (selected_paths->data);

      gtk_tree_model_get_iter (params->model, &params->iter_where_insert, priv->path_where_insert);
      gtk_tree_model_get (params->model, &params->iter_where_insert, DATA_COMPOSITION_COLUMN_TYPE, &params->type, -1);
    }
    
    priv->selected_files = selected_files;

    priv->thread_params = params;
    g_thread_create ((GThreadFunc) thread_add_files_action, params, FALSE, NULL);
    
    g_list_foreach (selected_paths, (GFunc) gtk_tree_path_free, NULL);
    g_list_free (selected_paths);
  }
}

static void
action_clear (GtkAction * action, XfburnDataComposition * dc)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  
  GtkTreeModel *model;

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
  gtk_tree_store_clear (GTK_TREE_STORE (model));
  
  gtk_entry_set_text (GTK_ENTRY (priv->entry_volume_name), _(DATA_COMPOSITION_DEFAULT_NAME));

  xfburn_data_disc_usage_set_size (XFBURN_DATA_DISC_USAGE (priv->disc_usage), 0);
}

static void
cb_content_drag_data_get (GtkWidget * widget, GdkDragContext * dc,
                          GtkSelectionData * data, guint info, guint time, XfburnDataComposition * content)
{
  if (info == DATA_COMPOSITION_DND_TARGET_INSIDE) {
    GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
    GtkTreeModel *model;
    GList *selected_rows = NULL, *row = NULL;
    GList *references = NULL;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));

    row = selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);

    while (row) {
      GtkTreeRowReference *reference = NULL;
      GtkTreePath *temp;

      temp = (GtkTreePath *) row->data;
      reference = gtk_tree_row_reference_new (model, temp);
      gtk_tree_path_free (temp);

      references = g_list_prepend (references, reference);
      
      row = g_list_next (row);
    }

    g_list_free (selected_rows);

    gtk_selection_data_set (data, gdk_atom_intern ("XFBURN_TREE_PATHS", FALSE), 8, (const guchar *) &references,
                            sizeof (GList **));
  }
}

static void
set_modified (XfburnDataCompositionPrivate *priv)
{
  if (!(priv->modified)) {
    XfburnMainWindow *mainwin;
    GtkUIManager *ui_manager;
    GtkActionGroup *action_group;
  
    mainwin = xfburn_main_window_get_instance ();
    ui_manager = xfburn_main_window_get_ui_manager (mainwin);
  
    action_group = (GtkActionGroup *) gtk_ui_manager_get_action_groups (ui_manager)->data;
    
    /*
    action = gtk_action_group_get_action (action_group, "save-composition");
    gtk_action_set_sensitive (GTK_ACTION (action), TRUE);
  */
    priv->modified = TRUE;
  }
}

static gboolean
thread_add_file_to_list_with_name (const gchar *name, XfburnDataComposition * dc, 
                                   GtkTreeModel * model, const gchar * path,
                                   GtkTreeIter * iter, GtkTreeIter * insertion, GtkTreeViewDropPosition position)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  
  struct stat s;

  if ((stat (path, &s) == 0)) {
    gchar *basename = NULL;
    gchar *humansize = NULL;
    GtkTreeIter *parent = NULL;
    GtkTreePath *tree_path = NULL;

    if (!S_ISDIR (s.st_mode) && !S_ISREG (s.st_mode)) {
      return FALSE;
    }
    
    basename = g_path_get_basename (path);
    if ( (strlen (basename) > 1) && (basename[0] == '.') ) {
      /* FIXME: is this really what we want? */
      /* don't add hidden files/directories */

      g_free (basename);  
      return FALSE;
    }
    g_free (basename);
    
    xfburn_adding_progress_pulse (XFBURN_ADDING_PROGRESS (priv->progress));
    
    /* find parent */
    switch (position){
      case GTK_TREE_VIEW_DROP_BEFORE:
      case GTK_TREE_VIEW_DROP_AFTER:
      if (insertion) {
          GtkTreeIter iter_parent;
          
          gdk_threads_enter ();
          if (gtk_tree_model_iter_parent (model, &iter_parent, insertion)) {
            parent = g_new0 (GtkTreeIter, 1);
            memcpy (parent, &iter_parent, sizeof (GtkTreeIter));
          }
          gdk_threads_leave ();
        }
        break;
      case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
      case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
        parent = g_new0 (GtkTreeIter, 1);
        memcpy (parent, insertion, sizeof (GtkTreeIter));
        break;
    }
  
    /* check if the filename is valid */
    gdk_threads_enter ();
    if (parent) {
      tree_path = gtk_tree_model_get_path (model, parent);
      gtk_tree_path_down (tree_path);
    } else {
      tree_path = gtk_tree_path_new_first ();
    }
    gdk_threads_leave ();
    
    gdk_threads_enter ();
    if (file_exists_on_same_level (model, tree_path, FALSE, name)) {
      xfce_err (_("A file with the same name is already present in the composition"));

      gtk_tree_path_free (tree_path);
      gdk_threads_leave ();
      g_free (parent);
      return FALSE;
    }
    gtk_tree_path_free (tree_path);
    gdk_threads_leave ();
    
    /* new directory */
    if (S_ISDIR (s.st_mode)) {
      GDir *dir = NULL;
      GError *error = NULL;
      const gchar *filename = NULL;
      guint64 total_size = 4;

      dir = g_dir_open (path, 0, &error);
      if (!dir) {
        g_warning ("unable to open directory : %s", error->message);

        g_error_free (error);
        g_free (parent);
        
        return FALSE;
      }

      gdk_threads_enter ();
      gtk_tree_store_append (GTK_TREE_STORE (model), iter, parent);

      gtk_tree_store_set (GTK_TREE_STORE (model), iter,
                          DATA_COMPOSITION_COLUMN_ICON, icon_directory,
                          DATA_COMPOSITION_COLUMN_CONTENT, name,
                          DATA_COMPOSITION_COLUMN_TYPE, DATA_COMPOSITION_TYPE_DIRECTORY, 
                          DATA_COMPOSITION_COLUMN_SIZE, (guint64) 4, -1);
      xfburn_data_disc_usage_add_size (XFBURN_DATA_DISC_USAGE (priv->disc_usage), (guint64) 4);
      gdk_threads_leave ();

      while ((filename = g_dir_read_name (dir))) {
        GtkTreeIter new_iter;
        gchar *new_path = NULL;

        new_path = g_build_filename (path, filename, NULL);      
        if (new_path) {
          guint64 size;

          if (thread_add_file_to_list (dc, model, new_path, &new_iter, iter, GTK_TREE_VIEW_DROP_INTO_OR_AFTER)) {
            gdk_threads_enter ();
            gtk_tree_model_get (model, &new_iter, DATA_COMPOSITION_COLUMN_SIZE, &size, -1);
            gdk_threads_leave ();
            total_size += size; 
          }
          
          g_free (new_path);
        }
      }

      humansize = xfburn_humanreadable_filesize (total_size);
      gdk_threads_enter ();
      gtk_tree_store_set (GTK_TREE_STORE (model), iter,
                          DATA_COMPOSITION_COLUMN_HUMANSIZE, humansize, DATA_COMPOSITION_COLUMN_SIZE, total_size, -1);
      gdk_threads_leave ();

      g_dir_close (dir);
    }
    /* new file */
    else if (S_ISREG (s.st_mode)) {
#ifdef HAVE_THUNAR_VFS
      GdkScreen *screen;
      GtkIconTheme *icon_theme;
      ThunarVfsMimeDatabase *mime_database = NULL;
      ThunarVfsMimeInfo *mime_info = NULL;
      const gchar *mime_icon_name = NULL;
      GdkPixbuf *mime_icon = NULL;
      gint x,y;
	  
      gdk_threads_enter ();
      screen = gtk_widget_get_screen (GTK_WIDGET (dc));
      icon_theme = gtk_icon_theme_get_for_screen (screen);
      
      mime_database = thunar_vfs_mime_database_get_default ();
      mime_info = thunar_vfs_mime_database_get_info_for_file (mime_database, path, NULL);
          
      gtk_icon_size_lookup (GTK_ICON_SIZE_SMALL_TOOLBAR, &x, &y);
      mime_icon_name = thunar_vfs_mime_info_lookup_icon_name (mime_info, icon_theme);
      mime_icon = gtk_icon_theme_load_icon (icon_theme, mime_icon_name, x, 0, NULL);
#endif
	
      gtk_tree_store_append (GTK_TREE_STORE (model), iter, parent);

      humansize = xfburn_humanreadable_filesize (s.st_size);

#ifdef HAVE_THUNAR_VFS
      gtk_tree_store_set (GTK_TREE_STORE (model), iter,
                          DATA_COMPOSITION_COLUMN_ICON, (G_IS_OBJECT (mime_icon) ? mime_icon : icon_file),
                          DATA_COMPOSITION_COLUMN_CONTENT, name,
                          DATA_COMPOSITION_COLUMN_HUMANSIZE, humansize,
                          DATA_COMPOSITION_COLUMN_SIZE, (guint64) s.st_size, DATA_COMPOSITION_COLUMN_PATH, path,
                          DATA_COMPOSITION_COLUMN_TYPE, DATA_COMPOSITION_TYPE_FILE, -1);
#else
      gtk_tree_store_set (GTK_TREE_STORE (model), iter,
                          DATA_COMPOSITION_COLUMN_ICON, icon_file,
                          DATA_COMPOSITION_COLUMN_CONTENT, name,
                          DATA_COMPOSITION_COLUMN_HUMANSIZE, humansize,
                          DATA_COMPOSITION_COLUMN_SIZE, (guint64) s.st_size, DATA_COMPOSITION_COLUMN_PATH, path,
                          DATA_COMPOSITION_COLUMN_TYPE, DATA_COMPOSITION_TYPE_FILE, -1);
#endif

      xfburn_data_disc_usage_add_size (XFBURN_DATA_DISC_USAGE (priv->disc_usage), s.st_size);
#ifdef HAVE_THUNAR_VFS
      if (G_LIKELY (G_IS_OBJECT (mime_icon)))
        g_object_unref (mime_icon);
      thunar_vfs_mime_info_unref (mime_info);
      g_object_unref (mime_database);
      gdk_threads_leave ();
#endif
    }
    g_free (humansize);
    g_free (parent);

    set_modified (priv);
    return TRUE;
  }
  
  return FALSE;
}

/* thread entry point */
static void
thread_add_files_cli (ThreadAddFilesCLIParams *params)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (params->dc);
  GtkTreeIter iter;

  GtkTreeModel *model;
  int i;
  gchar *full_path = NULL;

  gdk_threads_enter ();
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
  gdk_threads_leave ();

  for (i=0; i<params->filec; i++) {
    full_path = g_build_filename (params->filenames[i], NULL);
    g_message ("Adding %s to the data composition... (might take a while)", full_path);
    thread_add_file_to_list (params->dc, model, full_path, &iter, NULL, GTK_TREE_VIEW_DROP_AFTER);  
    g_free (full_path);
  }
  xfburn_adding_progress_done (XFBURN_ADDING_PROGRESS (priv->progress));
}

static gboolean
show_add_home_question_dialog ()
{
  gboolean ok = TRUE;

  gdk_threads_enter ();
  DBG ("Adding home directory");
  ok = xfburn_ask_yes_no (GTK_MESSAGE_WARNING, ((const gchar *) _("Adding home directory")),
                          _("You are about to add your home directory to the composition. " \
                            "This is likely to take a very long time, and also to be too big to fit on one disc.\n\n" \
                            "Are you sure you want to proceed?")
                         );

  gdk_threads_leave ();

  return ok;
}

/* thread entry point */
static void
thread_add_files_action (ThreadAddFilesActionParams *params)
{
  XfburnDataComposition *dc = params->dc;
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  GtkTreeModel *model = params->model;
  GtkTreeIter iter_where_insert = params->iter_where_insert;
  GtkTreePath *path_where_insert = priv->path_where_insert;
  const gchar * file = NULL;


  file = strtok (priv->selected_files, "\n");
  while (file) {
    GtkTreeIter iter;
    gchar *full_path = NULL;
    
    if (g_str_has_prefix (file, "file://"))
      full_path = g_build_filename (&file[7], NULL);
    else if (g_str_has_prefix (file, "file:"))
      full_path = g_build_filename (&file[5], NULL);
    else
      full_path = g_build_filename (file, NULL);

    if (full_path[strlen (full_path) - 1] == '\r')
      full_path[strlen (full_path) - 1] = '\0';

    if (strcmp (full_path, g_getenv ("HOME")) == 0) {
      if (!show_add_home_question_dialog ()) {
        g_free (full_path);
        break;
      }
    }

    /* add files to the disc content */
    if (params->type == DATA_COMPOSITION_TYPE_DIRECTORY) {
      guint64 old_size, size;
      gchar *humansize = NULL;
      
      thread_add_file_to_list (dc, model, full_path, &iter, &iter_where_insert, GTK_TREE_VIEW_DROP_INTO_OR_AFTER);
      gdk_threads_enter ();
      gtk_tree_view_expand_row (GTK_TREE_VIEW (priv->content), path_where_insert, FALSE);
      
      /* update parent directory size */
      gtk_tree_model_get (model, &iter_where_insert, DATA_COMPOSITION_COLUMN_SIZE, &old_size, -1);
      gtk_tree_model_get (model, &iter, DATA_COMPOSITION_COLUMN_SIZE, &size, -1);
      gdk_threads_leave ();
      
      humansize = xfburn_humanreadable_filesize (old_size + size);
      
      gdk_threads_enter ();
      gtk_tree_store_set (GTK_TREE_STORE (model), &iter_where_insert, 
                          DATA_COMPOSITION_COLUMN_HUMANSIZE, humansize,
                          DATA_COMPOSITION_COLUMN_SIZE, old_size + size, -1);
      gdk_threads_leave ();
      
      g_free (humansize);
    } else if (params->type == DATA_COMPOSITION_TYPE_FILE) {
      GtkTreeIter parent;
      gboolean has_parent;

      gdk_threads_enter ();
      has_parent = gtk_tree_model_iter_parent (model, &parent, &iter_where_insert);
      gdk_threads_leave ();
      
      if (has_parent)
        thread_add_file_to_list (dc, model, full_path, &iter, &parent, GTK_TREE_VIEW_DROP_INTO_OR_AFTER);  
      else 
        thread_add_file_to_list (dc, model, full_path, &iter, NULL, GTK_TREE_VIEW_DROP_AFTER);  
    } else {
      thread_add_file_to_list (dc, model, full_path, &iter, NULL, GTK_TREE_VIEW_DROP_AFTER);  
    }
    
    g_free (full_path);

    file = strtok (NULL, "\n");
  }
  xfburn_adding_progress_done (XFBURN_ADDING_PROGRESS (priv->progress));
}

static gboolean
thread_add_file_to_list (XfburnDataComposition * dc, GtkTreeModel * model, 
                         const gchar * path, GtkTreeIter * iter, GtkTreeIter * insertion, GtkTreeViewDropPosition position)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  struct stat s;
  gboolean ret = FALSE;
  
  if (xfburn_adding_progress_is_aborted (XFBURN_ADDING_PROGRESS (priv->progress))) {
    DBG ("Adding aborted");
    xfburn_adding_progress_done (XFBURN_ADDING_PROGRESS (priv->progress));
    /* FIXME: does this properly release the resources allocated in this thread? */
    g_thread_exit (NULL);
  }

  if ((stat (path, &s) == 0)) {
    gchar *basename = NULL;

    basename = g_path_get_basename (path);
    
    ret = thread_add_file_to_list_with_name (basename, dc, model, path, iter, insertion, position);
    
    g_free (basename);
  }

  return ret;
}

static gboolean
copy_entry_to (XfburnDataComposition *dc, GtkTreeIter *src, GtkTreeIter *dest, GtkTreeViewDropPosition position)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  
  gboolean ret = FALSE;
  
  GtkTreeModel *model;
  GtkTreeIter iter_new;
  
  GdkPixbuf *icon = NULL;
  gchar *name = NULL;
  gchar *humansize = NULL;
  guint64 size = 0;
  gchar *path = NULL;
  DataCompositionEntryType type;

  GtkTreePath *path_level = NULL;
  
  guint n_children = 0;
  guint i;
  GtkTreePath *path_src = NULL;
  
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
  
  gtk_tree_model_get (model, src, DATA_COMPOSITION_COLUMN_ICON, &icon, DATA_COMPOSITION_COLUMN_CONTENT, &name,
                      DATA_COMPOSITION_COLUMN_HUMANSIZE, &humansize, DATA_COMPOSITION_COLUMN_SIZE, &size,
                      DATA_COMPOSITION_COLUMN_PATH, &path, DATA_COMPOSITION_COLUMN_TYPE, &type, -1);
  
  switch (position) {
    case GTK_TREE_VIEW_DROP_BEFORE:
    case GTK_TREE_VIEW_DROP_AFTER:
      gtk_tree_store_insert_before (GTK_TREE_STORE (model), &iter_new, NULL, dest);
      break;
    case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
    case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
      if (dest) {
        path_level = gtk_tree_model_get_path (model, dest);
        gtk_tree_path_down (path_level);
      } else {
        path_level = gtk_tree_path_new_first ();
      }
    
      if (file_exists_on_same_level (model, path_level, FALSE, name)) {
        xfce_warn (_("A file named \"%s\" already exists in this directory, the file hasn't been added"), name);
        goto cleanup;
      }
      
      gtk_tree_path_free (path_level);
      
      gtk_tree_store_append (GTK_TREE_STORE (model), &iter_new, dest);                          
      break;
  }
  
  gtk_tree_store_set (GTK_TREE_STORE (model), &iter_new, DATA_COMPOSITION_COLUMN_ICON, icon, 
                      DATA_COMPOSITION_COLUMN_CONTENT, name, DATA_COMPOSITION_COLUMN_HUMANSIZE, humansize,
                      DATA_COMPOSITION_COLUMN_SIZE, size, DATA_COMPOSITION_COLUMN_PATH, path,
                      DATA_COMPOSITION_COLUMN_TYPE, type, -1);
    
  /* copy children */
  n_children = gtk_tree_model_iter_n_children (model, src);

  for (i = 0; i < n_children; i++) {
    GtkTreeIter iter_child;

    if (gtk_tree_model_iter_nth_child (model, &iter_child, src, i))
      copy_entry_to (dc, &iter_child, &iter_new, GTK_TREE_VIEW_DROP_INTO_OR_AFTER);
  }

  path_src = gtk_tree_model_get_path (model, src);
  if (n_children > 0 && gtk_tree_view_row_expanded (GTK_TREE_VIEW (priv->content), path_src)) {
    GtkTreePath *path_new = NULL;
    
    path_new = gtk_tree_model_get_path (model, &iter_new);
    gtk_tree_view_expand_row (GTK_TREE_VIEW (priv->content), path_new, FALSE);
    
    gtk_tree_path_free (path_new);
  }
  gtk_tree_path_free (path_src);
  
  ret =  TRUE;
  
cleanup:
  if (G_LIKELY (G_IS_OBJECT (icon)))
    g_object_unref (icon);
  g_free (name);
  g_free (humansize);
  g_free (path);
  
  return ret;
}

static void
cb_content_drag_data_rcv (GtkWidget * widget, GdkDragContext * dc, guint x, guint y, GtkSelectionData * sd,
                          guint info, guint t, XfburnDataComposition * composition)
{    
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (composition);
  
  GtkTreeModel *model;
  GtkTreePath *path_where_insert = NULL;
  GtkTreeViewDropPosition position;
  GtkTreeIter iter_where_insert;

  g_return_if_fail (sd);
  g_return_if_fail (sd->data);
  
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
  
  gtk_tree_view_get_dest_row_at_pos (GTK_TREE_VIEW (widget), x, y, &path_where_insert, &position);
  
  xfburn_busy_cursor (priv->content);

  /* move a selection inside of the composition window */
  if (sd->target == gdk_atom_intern ("XFBURN_TREE_PATHS", FALSE)) {
    GList *row = NULL, *selected_rows = NULL;
    GtkTreeIter *iter = NULL;
    DataCompositionEntryType type_dest = -1;
    
    xfburn_adding_progress_show (XFBURN_ADDING_PROGRESS (priv->progress));

    row = selected_rows = *((GList **) sd->data);
    
    if (path_where_insert) {      
      gtk_tree_model_get_iter (model, &iter_where_insert, path_where_insert);
      iter = &iter_where_insert;
      
      gtk_tree_model_get (model, &iter_where_insert, DATA_COMPOSITION_COLUMN_TYPE, &type_dest, -1);
      
      if (type_dest == DATA_COMPOSITION_TYPE_FILE) {
        if (position == GTK_TREE_VIEW_DROP_INTO_OR_AFTER)
          position = GTK_TREE_VIEW_DROP_AFTER;
        else if (position == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE)
          position = GTK_TREE_VIEW_DROP_BEFORE;
      }      
    } else {
      position = GTK_TREE_VIEW_DROP_INTO_OR_AFTER;
    }
    
    /* copy selection */
    while (row) {
      GtkTreePath *path_src = NULL;
      GtkTreeIter iter_src;
      GtkTreeRowReference *reference = NULL;
      DataCompositionEntryType type;
      guint64 size = 0;
      
      reference = (GtkTreeRowReference *) row->data;
    
      path_src = gtk_tree_row_reference_get_path (reference);
      if (!path_src) {
        gtk_tree_row_reference_free (reference);
        
        row = g_list_next (row);
        continue;
      }
      
      if (path_where_insert && (position == GTK_TREE_VIEW_DROP_AFTER || position == GTK_TREE_VIEW_DROP_BEFORE) 
          && (gtk_tree_path_get_depth (path_where_insert) == gtk_tree_path_get_depth (path_src))) {
          gtk_tree_path_free (path_src);
          gtk_tree_row_reference_free (reference);
      
          row = g_list_next (row);
          continue;
      }

      if (path_where_insert && type == DATA_COMPOSITION_TYPE_DIRECTORY 
          && gtk_tree_path_is_descendant (path_where_insert, path_src)) {

        gtk_tree_path_free (path_src);
        gtk_tree_path_free (path_where_insert);
        gtk_tree_row_reference_free (reference);
            
        gtk_drag_finish (dc, FALSE, FALSE, t);
        return;
      }

      gtk_tree_model_get_iter (model, &iter_src, path_src);
      gtk_tree_model_get (model, &iter_src, DATA_COMPOSITION_COLUMN_TYPE, &type,
                          DATA_COMPOSITION_COLUMN_SIZE, &size, -1);
      
      /* copy entry */
      if (copy_entry_to (composition, &iter_src, iter, position)) {                
        GtkTreePath *path_parent = gtk_tree_path_copy (path_src);
        
        /* update new parent size */
        if (iter && (position == GTK_TREE_VIEW_DROP_INTO_OR_AFTER 
                   || position == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE)) {
          guint64 old_size = 0;
          gchar *parent_humansize = NULL;

          gtk_tree_model_get (model, iter, DATA_COMPOSITION_COLUMN_SIZE, &old_size, -1);
            
          parent_humansize = xfburn_humanreadable_filesize (old_size + size);
          gtk_tree_store_set (GTK_TREE_STORE (model), iter, DATA_COMPOSITION_COLUMN_HUMANSIZE, parent_humansize,
                              DATA_COMPOSITION_COLUMN_SIZE, old_size + size, -1);
        
          g_free (parent_humansize);
        }
          
        if (dc->action == GDK_ACTION_MOVE) {       
          /* remove source entry */
          if (gtk_tree_path_up (path_parent) && path_where_insert && 
              !gtk_tree_path_is_descendant (path_where_insert, path_parent)) {
            /* update parent size and humansize */
            GtkTreeIter iter_parent;          
            guint64 old_size;
            gchar *parent_humansize = NULL;
            
            gtk_tree_model_iter_parent (model, &iter_parent, &iter_src);                
            gtk_tree_model_get (model, &iter_parent, DATA_COMPOSITION_COLUMN_SIZE, &old_size, -1);
         
            parent_humansize = xfburn_humanreadable_filesize (old_size - size);
            gtk_tree_store_set (GTK_TREE_STORE (model), &iter_parent, 
                                DATA_COMPOSITION_COLUMN_HUMANSIZE, parent_humansize,
                                DATA_COMPOSITION_COLUMN_SIZE, old_size - size, -1);
            g_free (parent_humansize);
          }
        
          gtk_tree_store_remove (GTK_TREE_STORE (model), &iter_src);
        } else {
          xfburn_data_disc_usage_add_size (XFBURN_DATA_DISC_USAGE (priv->disc_usage), size);
        }
        
        gtk_tree_path_free (path_parent);
      }

      gtk_tree_path_free (path_src);
      gtk_tree_row_reference_free (reference);
      
      row = g_list_next (row);
    }
    
    g_list_free (selected_rows);    
    gtk_drag_finish (dc, TRUE, FALSE, t);
    
    if (path_where_insert)
      gtk_tree_path_free (path_where_insert);  
    gtk_widget_hide (priv->progress);
    xfburn_default_cursor (priv->content);
  }
  /* drag from the file selector */
  else if (sd->target == gdk_atom_intern ("text/plain", FALSE)) {
    ThreadAddFilesDragParams *params;
    const gchar *file = NULL;

    xfburn_adding_progress_show (XFBURN_ADDING_PROGRESS (priv->progress));
    
    file = strtok ((gchar *) sd->data, "\n");
    while (file) {
      gchar *full_path;

      if (g_str_has_prefix (file, "file://"))
        full_path = g_build_filename (&file[7], NULL);
      else if (g_str_has_prefix ((gchar *) sd->data, "file:"))
        full_path = g_build_filename (&file[5], NULL);
      else
        full_path = g_build_filename (file, NULL);

      if (full_path[strlen (full_path) - 1] == '\r')
        full_path[strlen (full_path) - 1] = '\0';

      /* remember path to add it later in another thread */
      priv->full_paths_to_add = g_list_append (priv->full_paths_to_add, full_path);

      file = strtok (NULL, "\n");
    }

    priv->full_paths_to_add = g_list_reverse (priv->full_paths_to_add);
    priv->path_where_insert = path_where_insert;

    params = g_new (ThreadAddFilesDragParams, 1);
    params->composition = composition;
    params->position = position;
    params->widget = widget;

    /* append a dummy row so that gtk doesn't freak out */
    gtk_tree_store_append (GTK_TREE_STORE (model), &params->iter_dummy, NULL);

    priv->thread_params = params;
    g_thread_create ((GThreadFunc) thread_add_files_drag, params, FALSE, NULL);

    gtk_drag_finish (dc, TRUE, FALSE, t);
  } 
  else if (sd->target == gdk_atom_intern ("text/uri-list", FALSE)) {
#ifdef HAVE_THUNAR_VFS
    GList *vfs_paths = NULL;
    GList *vfs_path;
    GError *error = NULL;
    gchar *full_path;

    vfs_paths = thunar_vfs_path_list_from_string ((gchar *) sd->data, &error);

    if (G_LIKELY (vfs_paths != NULL)) {
      ThreadAddFilesDragParams *params;
      priv->full_paths_to_add = NULL;
      for (vfs_path = vfs_paths; vfs_path != NULL; vfs_path = g_list_next (vfs_path)) {
        ThunarVfsPath *path = THUNAR_VFS_PATH (vfs_path->data);
        if (thunar_vfs_path_get_scheme (path) != THUNAR_VFS_PATH_SCHEME_FILE)
          continue;
        full_path = thunar_vfs_path_dup_string (path);
        g_debug ("adding uri path: %s", full_path);
        priv->full_paths_to_add = g_list_prepend (priv->full_paths_to_add, full_path);
      }
      thunar_vfs_path_list_free (vfs_paths);

      priv->full_paths_to_add = g_list_reverse (priv->full_paths_to_add);
      priv->path_where_insert = path_where_insert;

      params = g_new (ThreadAddFilesDragParams, 1);
      params->composition = composition;
      params->position = position;
      params->widget = widget;

      /* append a dummy row so that gtk doesn't freak out */
      gtk_tree_store_append (GTK_TREE_STORE (model), &params->iter_dummy, NULL);

      priv->thread_params = params;
      g_thread_create ((GThreadFunc) thread_add_files_drag, params, FALSE, NULL);

      gtk_drag_finish (dc, TRUE, FALSE, t);
    } else {
      if (G_UNLIKELY (error != NULL))
        g_warning ("text/uri-list drag failed because '%s'", error->message);
      else
        g_warning("There were no files in the uri list!");
      gtk_drag_finish (dc, FALSE, FALSE, t);
      xfburn_default_cursor (priv->content);
    }
#else
    g_warning ("Receiving this type of drag and drop requires thunar-vfs support, sorry!");
    gtk_drag_finish (dc, FALSE, FALSE, t);
    xfburn_default_cursor (priv->content);
#endif
  } 
  else {
    g_warning ("Trying to receive an unsupported drag target, this should not happen.");
    gtk_drag_finish (dc, FALSE, FALSE, t);
    xfburn_default_cursor (priv->content);
  }
}

/* thread entry point */
static void
thread_add_files_drag (ThreadAddFilesDragParams *params)
{
  XfburnDataComposition *composition = params->composition;
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (composition);

  GtkTreeViewDropPosition position = params->position;
  GtkWidget *widget = params->widget;

  GtkTreeModel *model;
  GtkTreeIter iter_where_insert;
  GList *files = priv->full_paths_to_add;

  gdk_threads_enter ();
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

  /* remove the dummy row again */
  gtk_tree_store_remove (GTK_TREE_STORE (model), &params->iter_dummy);
  gdk_threads_leave ();

  for (; files; files = g_list_next (files)) {
    gchar *full_path = (gchar *) files->data;
    GtkTreeIter iter;

    if (priv->path_where_insert) {
      gdk_threads_enter ();
      gtk_tree_model_get_iter (model, &iter_where_insert, priv->path_where_insert);
      gdk_threads_leave ();
      
      if (thread_add_file_to_list (composition, model, full_path, &iter, &iter_where_insert, position)) {
        if (position == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE 
            || position == GTK_TREE_VIEW_DROP_INTO_OR_AFTER)
          gdk_threads_enter ();
          gtk_tree_view_expand_row (GTK_TREE_VIEW (widget), priv->path_where_insert, FALSE);
          gdk_threads_leave ();
      }
      
    } else  {
      thread_add_file_to_list (composition, model, full_path, &iter, NULL, position);
    }
  }
  xfburn_adding_progress_done (XFBURN_ADDING_PROGRESS (priv->progress));
}

static void
fill_image_with_composition (GtkTreeModel *model, IsoImage *image, IsoDir * parent, GtkTreeIter *iter)
{
  do {
      DataCompositionEntryType type;
      gchar *name = NULL;
      gchar *src = NULL;
      IsoNode *node = NULL;
      IsoDir *dir = NULL;
      int r;
      
      gtk_tree_model_get (model, iter, DATA_COMPOSITION_COLUMN_TYPE, &type,
			  DATA_COMPOSITION_COLUMN_CONTENT, &name, DATA_COMPOSITION_COLUMN_PATH, &src, -1);

      if (type == DATA_COMPOSITION_TYPE_DIRECTORY)
        r = iso_tree_add_new_dir (parent, name, &dir);
      else
        r = iso_tree_add_node (image, parent, src, &node);

      if (r < 0) {
        if (r == ISO_NULL_POINTER)
          g_error ("Failed adding %s as a node to the image: null pointer!", src);
        else if (r == ISO_NODE_NAME_NOT_UNIQUE)
          g_error ("Failed adding %s as a node to the image: node name not unique!", src);
        /* else if (r == ISO_MEM_ERROR)
          g_error ("Failed adding %s as a node to the image: memory error!", src); */
        else
          g_error ("Failed adding %s as a node to the image: code %d!", src, r);
      }

      /* FIXME: do we need to call iso_node_ref on node? Probably not... */
      if (type != DATA_COMPOSITION_TYPE_DIRECTORY)
        iso_node_set_name (node, name);

      g_free (name);
      g_free (src);

      if (type == DATA_COMPOSITION_TYPE_DIRECTORY && gtk_tree_model_iter_has_child (model, iter)) {
	GtkTreeIter child;

        /* FIXME: this should not cause a sigfault... 
        if (iso_node_get_type(node) != LIBISO_DIR)
            g_error ("Expected %s to be a directory, but it isn't...\n", src);
        */

	gtk_tree_model_iter_children (model, &child, iter);
	fill_image_with_composition (model, image, dir, &child);
      }
  } while (gtk_tree_model_iter_next (model, iter));
}

static IsoImage *
generate_iso_image (XfburnDataComposition * dc)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  IsoImage *image = NULL;
  GtkTreeModel *model;
  GtkTreeIter iter;

  iso_image_new (gtk_entry_get_text (GTK_ENTRY (priv->entry_volume_name)), &image);
  iso_image_set_application_id (image, "Xfburn");
  iso_image_set_data_preparer_id (image, "Xfburn");

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
  if (gtk_tree_model_get_iter_first (model, &iter)) {
    fill_image_with_composition (model, image, iso_image_get_root (image), &iter);
  }

  return image;
}

/****************/
/* loading code */
/****************/
typedef struct
{
  gboolean started;
  XfburnDataComposition *dc;
  GQueue *queue_iter;
} LoadParserStruct;

static gint
_find_attribute (const gchar ** attribute_names, const gchar * attr)
{
  gint i;

  for (i = 0; attribute_names[i]; i++) {
    if (!strcmp (attribute_names[i], attr))
      return i;
  }

  return -1;
}

static void
load_composition_start (GMarkupParseContext * context, const gchar * element_name,
                        const gchar ** attribute_names, const gchar ** attribute_values,
                        gpointer data, GError ** error)
{
  LoadParserStruct * parserinfo = (LoadParserStruct *) data;
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (parserinfo->dc);

  
  if (!(parserinfo->started) && !strcmp (element_name, "xfburn-composition"))
    parserinfo->started = TRUE;
  else if (!(parserinfo->started))
    return;

  if (!strcmp (element_name, "file")) {
    int i, j;

    if ((i = _find_attribute (attribute_names, "name")) != -1 &&
        (j = _find_attribute (attribute_names, "source")) != -1) {
      //GtkTreeIter iter;
      GtkTreeIter *parent;
      GtkTreeModel *model;

      model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
      parent = g_queue_peek_head (parserinfo->queue_iter);
          
      g_error ("This method needs to get fixed, and does not work right now!");
      /*
      add_file_to_list_with_name (attribute_values[i], parserinfo->dc, model, attribute_values[j], &iter, 
                                  parent, GTK_TREE_VIEW_DROP_INTO_OR_AFTER);
      */
    }
  } else if (!strcmp (element_name, "directory")) {
    int i, j;

    if ((i = _find_attribute (attribute_names, "name")) != -1 &&
        (j = _find_attribute (attribute_names, "source")) != -1) {
      //GtkTreeIter iter;
      GtkTreeModel *model;

      model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
      
      //add_directory_to_list (attribute_values[i], parserinfo->dc, model, attribute_values[j], &iter, parent);    
    }
  }
}

static void
load_composition_end (GMarkupParseContext * context, const gchar * element_name, gpointer user_data, GError ** error)
{
  LoadParserStruct *parserinfo = (LoadParserStruct *) user_data;
  
  if (!parserinfo->started)
    return;
  
  if (!strcmp (element_name, "xfburn-composition"))
    parserinfo->started = FALSE;
  
  if (!strcmp (element_name, "directory"))
    parserinfo->queue_iter = g_queue_pop_head (parserinfo->queue_iter);
}

static void
load_from_file (XfburnComposition * composition, const gchar * filename)
{
  gchar *file_contents = NULL;
  GMarkupParseContext *gpcontext = NULL;
  struct stat st;
  LoadParserStruct parserinfo;
  GMarkupParser gmparser = {
    load_composition_start, load_composition_end, NULL, NULL, NULL
  };
  GError *err = NULL;
#ifdef HAVE_MMAP
  gint fd = -1;
  void *maddr = NULL;
#endif
  g_return_if_fail (filename != NULL);
  if (stat (filename, &st) < 0) {
    g_warning ("Unable to open %s", filename);
    goto cleanup;
  }

#ifdef HAVE_MMAP
  fd = open (filename, O_RDONLY, 0);
  if (fd < 0)
    goto cleanup;
  maddr = mmap (NULL, st.st_size, PROT_READ, MAP_FILE | MAP_SHARED, fd, 0);
  if (maddr)
    file_contents = maddr;
#endif
  if (!file_contents && !g_file_get_contents (filename, &file_contents, NULL, &err)) {
    if (err) {
      g_warning ("Unable to read file '%s' (%d): %s", filename, err->code, err->message);
      g_error_free (err);
    }
    goto cleanup;
  }

  parserinfo.started = FALSE;
  parserinfo.dc = XFBURN_DATA_COMPOSITION (composition);
  parserinfo.queue_iter = g_queue_new ();
  gpcontext = g_markup_parse_context_new (&gmparser, 0, &parserinfo, NULL);
  if (!g_markup_parse_context_parse (gpcontext, file_contents, st.st_size, &err)) {
    g_warning ("Error parsing composition (%d): %s", err->code, err->message);
    g_error_free (err);
    goto cleanup;
  }

  if (g_markup_parse_context_end_parse (gpcontext, NULL)) {
    DBG ("parsed");
  }

  g_queue_free (parserinfo.queue_iter);
  
cleanup:
  if (gpcontext)
    g_markup_parse_context_free (gpcontext);
#ifdef HAVE_MMAP
  if (maddr) {
    munmap (maddr, st.st_size);
    file_contents = NULL;
  }
  if (fd > -1)
    close (fd);
#endif
  if (file_contents)
    g_free (file_contents);
}


/***************/
/* saving code */
/***************/
typedef struct
{
  FILE *file_content;
  gint last_depth;
} CompositionSaveInfo;

static gboolean
foreach_save (GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter, CompositionSaveInfo *info)
{
  gchar *space = NULL;
  gint i;
  gchar *name = NULL;
  gchar *source_path = NULL;
  DataCompositionEntryType type;

  space = g_strnfill (gtk_tree_path_get_depth (path), '\t');
  
  for (i = info->last_depth; i > gtk_tree_path_get_depth (path); i--) {
    gchar *space2 = NULL;

    space2 = g_strnfill (i - 1, '\t');
    fprintf (info->file_content, "%s</directory>\n", space2);
    
    g_free (space2);
  }
  
  gtk_tree_model_get (model, iter, DATA_COMPOSITION_COLUMN_CONTENT, &name,
                      DATA_COMPOSITION_COLUMN_PATH, &source_path,
                      DATA_COMPOSITION_COLUMN_TYPE, &type, -1);
  
  fprintf (info->file_content, "%s", space);
  switch (type) {
  case DATA_COMPOSITION_TYPE_FILE:
    fprintf (info->file_content, "<file name=\"%s\" source=\"%s\" />\n", name, source_path);
    break;
  case DATA_COMPOSITION_TYPE_DIRECTORY:
    fprintf (info->file_content, "<directory name=\"%s\" source=\"%s\"", name, source_path);
  
    if (gtk_tree_model_iter_has_child (model, iter))
      fprintf (info->file_content, ">\n");
    else
      fprintf (info->file_content, "/>\n");

    break;
  }


  info->last_depth = gtk_tree_path_get_depth (path);
  
  g_free (space);
  g_free (name);
  g_free (source_path);

  return FALSE;
}

static void
save_to_file (XfburnComposition * composition)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (composition);
  FILE *file_content;
  GtkTreeModel *model;
  CompositionSaveInfo info = {};
  gint i;
    
  if (!(priv->filename)) {
    priv->filename = g_strdup ("/tmp/gna");
    
    g_signal_emit_by_name (G_OBJECT (composition), "name-changed", priv->filename);
  }
  
  file_content = fopen (priv->filename, "w+");
  fprintf (file_content, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n");
  fprintf (file_content, "<xfburn-composition version=\"0.1\">\n");

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->content));
  info.file_content = file_content;
  info.last_depth = 0;
  gtk_tree_model_foreach (model, (GtkTreeModelForeachFunc) foreach_save, &info);

  for (i = info.last_depth; i > 1; i--) {
    gchar *space2 = NULL;

    space2 = g_strnfill (i - 1, '\t');
    fprintf (info.file_content, "%s</directory>\n", space2);
    
    g_free (space2);
  }
    
  fprintf (file_content, "</xfburn-composition>\n");
  fclose (file_content);
}

/******************/
/* public methods */
/******************/
GtkWidget *
xfburn_data_composition_new (void)
{
  return g_object_new (xfburn_data_composition_get_type (), NULL);
}

void 
xfburn_data_composition_add_files (XfburnDataComposition *dc, int filec, char **filenames)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (dc);
  ThreadAddFilesCLIParams *params;
  
  if (filec > 0) {
    params = g_new (ThreadAddFilesCLIParams, 1);

    params->filenames = filenames;
    params->filec = filec;
    params->dc = dc;

    xfburn_adding_progress_show (XFBURN_ADDING_PROGRESS (priv->progress));
    xfburn_busy_cursor (priv->content);

    priv->thread_params = params;
    g_thread_create ((GThreadFunc) thread_add_files_cli, params, FALSE, NULL);
  }
}

void
xfburn_data_composition_hide_toolbar (XfburnDataComposition * composition)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (composition);
  
  gtk_widget_hide (priv->toolbar);
}

void
xfburn_data_composition_show_toolbar (XfburnDataComposition * composition)
{
  XfburnDataCompositionPrivate *priv = XFBURN_DATA_COMPOSITION_GET_PRIVATE (composition);
  
  gtk_widget_show (priv->toolbar);
}
