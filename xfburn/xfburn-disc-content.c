/*
 * Copyright (c) 2005 Jean-François Wauthy (pollux@xfce.org)
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

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>

#include <exo/exo.h>

#include "xfburn-disc-content.h"
#include "xfburn-disc-usage.h"
#include "xfburn-utils.h"

/* prototypes */
static void xfburn_disc_content_class_init (XfburnDiscContentClass *);
static void xfburn_disc_content_init (XfburnDiscContent *);
static void xfburn_disc_content_finalize (GObject * object);

static gint directory_tree_sortfunc (GtkTreeModel * model, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data);

static void xfburn_disc_content_action_clear (GtkAction *, XfburnDiscContent *);

static void cell_file_edited_cb (GtkCellRenderer * renderer, gchar * path, gchar * newtext, XfburnDiscContent * dc);

static void content_drag_data_rcv_cb (GtkWidget *, GdkDragContext *, guint, guint, GtkSelectionData *, guint, guint,
                                      XfburnDiscContent *);
static void content_drag_data_get_cb (GtkWidget * widget, GdkDragContext * dc, GtkSelectionData * data, guint info,
                                      guint time, XfburnDiscContent * content);

enum
{
  DISC_CONTENT_COLUMN_ICON,
  DISC_CONTENT_COLUMN_CONTENT,
  DISC_CONTENT_COLUMN_HUMANSIZE,
  DISC_CONTENT_COLUMN_SIZE,
  DISC_CONTENT_COLUMN_PATH,
  DISC_CONTENT_N_COLUMNS
};

struct XfburnDiscContentPrivate
{
  GtkActionGroup *action_group;
  GtkUIManager *ui_manager;

  GtkWidget *toolbar;
  GtkWidget *content;
  GtkWidget *disc_usage;
};

/* globals */
static GtkHPanedClass *parent_class = NULL;
static GtkActionEntry action_entries[] = {
  {"add-file", GTK_STOCK_ADD, N_("Add file(s)"), NULL, N_("Add the selected files to the CD"),},
  {"remove-file", GTK_STOCK_REMOVE, N_("Remove selected"), NULL, N_("Remove the selected files from the CD"),},
  {"clear", GTK_STOCK_CLEAR, N_("Clear"), NULL, N_("Clear the contents of the CD"),
   G_CALLBACK (xfburn_disc_content_action_clear),},
  {"import-session", "xfburn-import-session", N_("Import"), NULL, N_("Import existing session"),},
};
static const gchar *toolbar_actions[] = {
  "add-file",
  "remove-file",
  "clear",
  "import-session",
};

/***************************/
/* XfburnDiscContent class */
/***************************/
GtkType
xfburn_disc_content_get_type (void)
{
  static GtkType disc_content_type = 0;

  if (!disc_content_type) {
    static const GTypeInfo disc_content_info = {
      sizeof (XfburnDiscContentClass),
      NULL,
      NULL,
      (GClassInitFunc) xfburn_disc_content_class_init,
      NULL,
      NULL,
      sizeof (XfburnDiscContent),
      0,
      (GInstanceInitFunc) xfburn_disc_content_init
    };

    disc_content_type = g_type_register_static (GTK_TYPE_VBOX, "XfburnDiscContent", &disc_content_info, 0);
  }

  return disc_content_type;
}

static void
xfburn_disc_content_class_init (XfburnDiscContentClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);
  object_class->finalize = xfburn_disc_content_finalize;
}

static void
xfburn_disc_content_init (XfburnDiscContent * disc_content)
{
  ExoToolbarsModel *model_toolbar;
  gint toolbar_position;
  GtkWidget *scrolled_window;
  GtkTreeStore *model;
  GtkTreeViewColumn *column_file;
  GtkCellRenderer *cell_icon, *cell_file;
  GtkTreeSelection *selection;

  GtkTargetEntry gte_src[] = { {"XFBURN_TREE_PATHS", GTK_TARGET_SAME_WIDGET, DISC_CONTENT_DND_TARGET_INSIDE} };
  GtkTargetEntry gte_dest[] = { {"XFBURN_TREE_PATHS", GTK_TARGET_SAME_WIDGET, DISC_CONTENT_DND_TARGET_INSIDE},
  {"text/plain", 0, DISC_CONTENT_DND_TARGET_TEXT_PLAIN}
  };

  disc_content->priv = g_new0 (XfburnDiscContentPrivate, 1);

  /* create ui manager */
  disc_content->priv->action_group = gtk_action_group_new ("xfburn-main-window");
  gtk_action_group_set_translation_domain (disc_content->priv->action_group, GETTEXT_PACKAGE);
  gtk_action_group_add_actions (disc_content->priv->action_group, action_entries, G_N_ELEMENTS (action_entries),
                                GTK_WIDGET (disc_content));

  disc_content->priv->ui_manager = gtk_ui_manager_new ();
  gtk_ui_manager_insert_action_group (disc_content->priv->ui_manager, disc_content->priv->action_group, 0);

  /* toolbar */
  model_toolbar = exo_toolbars_model_new ();
  exo_toolbars_model_set_actions (model_toolbar, (gchar **) toolbar_actions, G_N_ELEMENTS (toolbar_actions));
  toolbar_position = exo_toolbars_model_add_toolbar (model_toolbar, -1, "content-toolbar");
  exo_toolbars_model_set_style (model_toolbar, GTK_TOOLBAR_BOTH, toolbar_position);

  exo_toolbars_model_add_item (model_toolbar, toolbar_position, -1, "add-file", EXO_TOOLBARS_ITEM_TYPE);
  exo_toolbars_model_add_item (model_toolbar, toolbar_position, -1, "remove-file", EXO_TOOLBARS_ITEM_TYPE);
  exo_toolbars_model_add_item (model_toolbar, toolbar_position, -1, "clear", EXO_TOOLBARS_ITEM_TYPE);
  exo_toolbars_model_add_separator (model_toolbar, toolbar_position, -1);
  exo_toolbars_model_add_item (model_toolbar, toolbar_position, -1, "import-session", EXO_TOOLBARS_ITEM_TYPE);


  disc_content->priv->toolbar = exo_toolbars_view_new_with_model (disc_content->priv->ui_manager, model_toolbar);
  gtk_box_pack_start (GTK_BOX (disc_content), disc_content->priv->toolbar, FALSE, FALSE, 0);
  gtk_widget_show (disc_content->priv->toolbar);

  /* content treeview */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);
  gtk_widget_show (scrolled_window);
  gtk_box_pack_start (GTK_BOX (disc_content), scrolled_window, TRUE, TRUE, 0);

  disc_content->priv->content = gtk_tree_view_new ();
  model = gtk_tree_store_new (DISC_CONTENT_N_COLUMNS, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING,
                              G_TYPE_UINT64, G_TYPE_STRING);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (model), DISC_CONTENT_COLUMN_CONTENT,
                                   directory_tree_sortfunc, NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model), DISC_CONTENT_COLUMN_CONTENT, GTK_SORT_ASCENDING);
  gtk_tree_view_set_model (GTK_TREE_VIEW (disc_content->priv->content), GTK_TREE_MODEL (model));
  gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (disc_content->priv->content), TRUE);
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (disc_content->priv->content));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
  gtk_widget_show (disc_content->priv->content);
  gtk_container_add (GTK_CONTAINER (scrolled_window), disc_content->priv->content);

  column_file = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column_file, _("Contents"));

  cell_icon = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column_file, cell_icon, FALSE);
  gtk_tree_view_column_set_attributes (column_file, cell_icon, "pixbuf", DISC_CONTENT_COLUMN_ICON, NULL);
  g_object_set (cell_icon, "xalign", 0.0, "ypad", 0, NULL);

  cell_file = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (cell_file), "editable", TRUE);
  gtk_tree_view_column_pack_start (column_file, cell_file, TRUE);
  gtk_tree_view_column_set_attributes (column_file, cell_file, "text", DISC_CONTENT_COLUMN_CONTENT, NULL);
  g_signal_connect (G_OBJECT (cell_file), "edited", G_CALLBACK (cell_file_edited_cb), disc_content);

  gtk_tree_view_append_column (GTK_TREE_VIEW (disc_content->priv->content), column_file);

  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (disc_content->priv->content), -1, _("Size"),
                                               gtk_cell_renderer_text_new (), "text", DISC_CONTENT_COLUMN_HUMANSIZE,
                                               NULL);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (disc_content->priv->content), -1, _("Full Path"),
                                               gtk_cell_renderer_text_new (), "text", DISC_CONTENT_COLUMN_PATH, NULL);

  /* disc usage */
  disc_content->priv->disc_usage = xfburn_disc_usage_new ();
  gtk_box_pack_start (GTK_BOX (disc_content), disc_content->priv->disc_usage, FALSE, FALSE, 5);
  gtk_widget_show (disc_content->priv->disc_usage);

  /* set up DnD */
  gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (disc_content->priv->content), GDK_BUTTON1_MASK, gte_src,
                                          G_N_ELEMENTS (gte_src), GDK_ACTION_MOVE);
  g_signal_connect (G_OBJECT (disc_content->priv->content), "drag-data-get", G_CALLBACK (content_drag_data_get_cb),
                    disc_content);
  gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (disc_content->priv->content), gte_dest, G_N_ELEMENTS (gte_dest),
                                        GDK_ACTION_COPY | GDK_ACTION_MOVE);
  g_signal_connect (G_OBJECT (disc_content->priv->content), "drag-data-received", G_CALLBACK (content_drag_data_rcv_cb),
                    disc_content);
}

static void
xfburn_disc_content_finalize (GObject * object)
{
  XfburnDiscContent *cobj;
  cobj = XFBURN_DISC_CONTENT (object);

  g_free (cobj->priv);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* internals */
static gint
directory_tree_sortfunc (GtkTreeModel * model, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data)
{
  /* adapted from gnomebaker */
  gchar *aname, *bname, *apath, *bpath;
  gboolean aisdir = FALSE;
  gboolean bisdir = FALSE;
  gint result = 0;

  gtk_tree_model_get (model, a, DISC_CONTENT_COLUMN_CONTENT, &aname, DISC_CONTENT_COLUMN_PATH, &apath, -1);
  gtk_tree_model_get (model, b, DISC_CONTENT_COLUMN_CONTENT, &bname, DISC_CONTENT_COLUMN_PATH, &bpath, -1);

  aisdir = g_file_test (apath, G_FILE_TEST_IS_DIR);
  bisdir = g_file_test (bpath, G_FILE_TEST_IS_DIR);

  if (aisdir && !bisdir)
    result = -1;
  else if (!aisdir && bisdir)
    result = 1;
  else
    result = g_ascii_strcasecmp (aname, bname);

  g_free (aname);
  g_free (apath);
  g_free (bname);
  g_free (bpath);

  return result;
}

static void
cell_file_edited_cb (GtkCellRenderer * renderer, gchar * path, gchar * newtext, XfburnDiscContent * dc)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreePath *real_path;

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (dc->priv->content));
  real_path = gtk_tree_path_new_from_string (path);

  if (gtk_tree_model_get_iter (model, &iter, real_path)) {
    gtk_tree_store_set (GTK_TREE_STORE (model), &iter, DISC_CONTENT_COLUMN_CONTENT, newtext, -1);
  }

  gtk_tree_path_free (real_path);
}

static void
xfburn_disc_content_action_clear (GtkAction * action, XfburnDiscContent * content)
{
  GtkTreeModel *model;

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (content->priv->content));
  gtk_tree_store_clear (GTK_TREE_STORE (model));

  xfburn_disc_usage_set_size (XFBURN_DISC_USAGE (content->priv->disc_usage), 0);
}

static void
add_content (XfburnDiscContent *content, GtkTreeModel *model, const gchar * full_path, GtkTreeViewDropPosition position, 
             GtkTreeIter * iter_where_insert, GtkTreeIter * iter)
{
  gchar *basename;
  struct stat s;
  
  basename = g_path_get_basename (full_path);

  if ((stat (full_path, &s) == 0)) {
    gchar *humansize = NULL;
    GdkPixbuf *icon_directory, *icon_file;
    gint x, y;

    gtk_icon_size_lookup (GTK_ICON_SIZE_BUTTON, &x, &y);
    icon_directory = xfce_themed_icon_load ("gnome-fs-directory", x);
    icon_file = xfce_themed_icon_load ("gnome-fs-regular", x);
    
    if (iter_where_insert) {
      switch (position) {
      case GTK_TREE_VIEW_DROP_BEFORE:
        gtk_tree_store_insert_before (GTK_TREE_STORE (model), iter, NULL, iter_where_insert);
        break;
      case GTK_TREE_VIEW_DROP_AFTER:
        gtk_tree_store_insert_after (GTK_TREE_STORE (model), iter, NULL, iter_where_insert);
        break;
      case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
      case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
        gtk_tree_store_append (GTK_TREE_STORE (model), iter, iter_where_insert);
      }
    }
    else
      gtk_tree_store_append (GTK_TREE_STORE (model), iter, NULL);
   
    humansize = xfburn_humanreadable_filesize (s.st_size);
    
    gtk_tree_store_set (GTK_TREE_STORE (model), iter,
                        DISC_CONTENT_COLUMN_ICON, icon_file,
                        DISC_CONTENT_COLUMN_CONTENT, basename,
                        DISC_CONTENT_COLUMN_HUMANSIZE, humansize,
                        DISC_CONTENT_COLUMN_SIZE, (guint64) s.st_size, DISC_CONTENT_COLUMN_PATH, full_path, -1);
    
    if ((s.st_mode & S_IFDIR)) {
      GDir *dir;
      const gchar *direntry;
      
      gtk_tree_store_set (GTK_TREE_STORE (model), iter, DISC_CONTENT_COLUMN_ICON, icon_directory, -1);
      
      dir = g_dir_open (full_path, 0, NULL);
      while ( (direntry = g_dir_read_name (dir)) ) {
        gchar *full_dir;
        GtkTreeIter newiter;
        
        full_dir = g_build_filename (full_path, G_DIR_SEPARATOR_S, direntry, NULL);
        add_content (content, model, full_dir, GTK_TREE_VIEW_DROP_INTO_OR_AFTER, iter, &newiter); 
        
        g_free (full_dir);
      }
      g_dir_close (dir);
    }     
    
    xfburn_disc_usage_add_size (XFBURN_DISC_USAGE (content->priv->disc_usage), s.st_size);
    
    if (icon_directory)
      g_object_unref (icon_directory);
    if (icon_file)
      g_object_unref (icon_file);
    
    g_free (humansize);
  }

  g_free (basename);
}

static void
content_drag_data_get_cb (GtkWidget * widget, GdkDragContext * dc,
                          GtkSelectionData * data, guint info, guint time, XfburnDiscContent * content)
{
  if (info == DISC_CONTENT_DND_TARGET_INSIDE) {
    GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
    GtkTreeModel *model;
    GList *selected_rows;
    gchar *all_paths;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));

    all_paths = g_strdup ("");
    selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);
    selected_rows = g_list_last (selected_rows);

    while (selected_rows) {
      gchar *current_path;
      gchar *temp;

      current_path = gtk_tree_path_to_string ((GtkTreePath *) selected_rows->data);

      temp = g_strconcat (current_path, "\n", all_paths, NULL);
      g_free (current_path);
      g_free (all_paths);
      all_paths = temp;

      selected_rows = g_list_previous (selected_rows);
    }

    selected_rows = g_list_first (selected_rows);
    g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
    g_list_free (selected_rows);

    gtk_selection_data_set (data, gdk_atom_intern ("XFBURN_TREE_PATHS", FALSE), 8, all_paths, sizeof (all_paths));

    g_free (all_paths);
  }
}

static void
content_drag_data_rcv_cb (GtkWidget * widget, GdkDragContext * dc, guint x, guint y, GtkSelectionData * sd,
                          guint info, guint t, XfburnDiscContent * content)
{
  GtkTreeModel *model;
  GtkTreeViewDropPosition position;
  GtkTreePath *path_where_insert;
  GtkTreeIter *iter_where_insert;

  g_return_if_fail (sd);
  g_return_if_fail (sd->data);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

  if (gtk_tree_view_get_dest_row_at_pos (GTK_TREE_VIEW (widget), x, y, &path_where_insert, &position)) {
    iter_where_insert = g_new0 (GtkTreeIter, 1);
    gtk_tree_model_get_iter (model, iter_where_insert, path_where_insert);
    gtk_tree_path_free (path_where_insert);
  }
  else
    iter_where_insert = NULL;

  if (sd->target == gdk_atom_intern ("XFBURN_TREE_PATHS", FALSE)) {
    const gchar *str_path;

    DBG ("DnD");
    str_path = strtok ((gchar *) sd->data, "\n");
    while (str_path) {

      str_path = strtok (NULL, "\n");
    }

    gtk_drag_finish (dc, TRUE, FALSE, t);
  }
  else if (sd->target == gdk_atom_intern ("text/plain", FALSE)) {
    const gchar *file;
    gchar *full_path;
    GtkTreeIter iter;
    GtkTreePath *path_new_iter;
    
    file = strtok ((gchar *) sd->data, "\n");
    while (file) {
      if (g_str_has_prefix (file, "file://"))
        full_path = g_build_filename (&file[7], NULL);
      else if (g_str_has_prefix (sd->data, "file:"))
        full_path = g_build_filename (&file[5], NULL);
      else
        full_path = g_build_filename (file, NULL);

      if (full_path[strlen (full_path) - 1] == '\r')
        full_path[strlen (full_path) - 1] = '\0';

      gtk_tree_view_set_model (GTK_TREE_VIEW (widget), NULL);
      add_content (content, model, full_path, position, iter_where_insert, &iter);
      gtk_tree_view_set_model (GTK_TREE_VIEW (widget), model);
      path_new_iter = gtk_tree_model_get_path (model, &iter);
      gtk_tree_view_expand_to_path (GTK_TREE_VIEW (content->priv->content), path_new_iter);
      gtk_tree_path_free (path_new_iter);
      
      file = strtok (NULL, "\n");
      g_free (full_path);
    }

    gtk_drag_finish (dc, TRUE, (dc->action == GDK_ACTION_MOVE), t);
  }

  g_free (iter_where_insert);
}

/* public methods */
GtkWidget *
xfburn_disc_content_new (void)
{
  return g_object_new (xfburn_disc_content_get_type (), NULL);
}

void
xfburn_disc_content_hide_toolbar (XfburnDiscContent * content)
{
  gtk_widget_hide (content->priv->toolbar);
}

void
xfburn_disc_content_show_toolbar (XfburnDiscContent * content)
{
  gtk_widget_show (content->priv->toolbar);
}
