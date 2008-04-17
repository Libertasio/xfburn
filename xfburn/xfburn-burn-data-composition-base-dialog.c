/* $Id$ */
/*
 *  Copyright (c) 2005-2007 Jean-François Wauthy (pollux@xfce.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef	HAVE_CONFIG_H
#include <config.h>
#endif /* !HAVE_CONFIG_H */

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <errno.h>

#include "xfburn-global.h"
#include "xfburn-utils.h"
#include "xfburn-settings.h"
#include "xfburn-stock.h"

#include "xfburn-device-box.h"
#include "xfburn-burn-data-composition-base-dialog.h"
#include "xfburn-progress-dialog.h"
#include "xfburn-perform-burn.h"

#define XFBURN_BURN_DATA_COMPOSITION_BASE_DIALOG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), XFBURN_TYPE_BURN_DATA_COMPOSITION_BASE_DIALOG, XfburnBurnDataCompositionBaseDialogPrivate))

typedef struct
{
  IsoImage *image;

  GtkWidget *frame_device;
  GtkWidget *device_box;
  GtkWidget *combo_mode;

  GtkWidget *check_eject;
  GtkWidget *check_burnfree;
  GtkWidget *check_only_iso;
  GtkWidget *hbox_iso;
  GtkWidget *entry_path_iso;
  GtkWidget *check_dummy;
} XfburnBurnDataCompositionBaseDialogPrivate;

enum {
  PROP_0,
  PROP_IMAGE
};

/* prototypes */
static void xfburn_burn_data_composition_base_dialog_class_init (XfburnBurnDataCompositionBaseDialogClass * klass);
static void xfburn_burn_data_composition_base_dialog_init (XfburnBurnDataCompositionBaseDialog * obj);
static void xfburn_burn_data_composition_base_dialog_finalize (GObject * object);

static void xfburn_burn_data_composition_base_dialog_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void xfburn_burn_data_composition_base_dialog_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);

static void cb_check_only_iso_toggled (GtkToggleButton * button, XfburnBurnDataCompositionBaseDialog * dialog);
static void cb_browse_iso (GtkButton * button, XfburnBurnDataCompositionBaseDialog * dialog);
static void cb_dialog_response (XfburnBurnDataCompositionBaseDialog * dialog, gint response_id,
                                XfburnBurnDataCompositionBaseDialogPrivate * priv);

/* globals */
static XfceTitledDialogClass *parent_class = NULL;

GtkType
xfburn_burn_data_composition_base_dialog_get_type ()
{
  static GtkType type = 0;

  if (type == 0) {
    static const GTypeInfo our_info = {
      sizeof (XfburnBurnDataCompositionBaseDialogClass),
      NULL,
      NULL,
      (GClassInitFunc) xfburn_burn_data_composition_base_dialog_class_init,
      NULL,
      NULL,
      sizeof (XfburnBurnDataCompositionBaseDialog),
      0,
      (GInstanceInitFunc) xfburn_burn_data_composition_base_dialog_init,
    };

    type = g_type_register_static (XFCE_TYPE_TITLED_DIALOG, "XfburnBurnDataCompositionBaseDialog", &our_info, 0);
  }

  return type;
}

static void
xfburn_burn_data_composition_base_dialog_class_init (XfburnBurnDataCompositionBaseDialogClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);
  g_type_class_add_private (klass, sizeof (XfburnBurnDataCompositionBaseDialogPrivate));
  
  object_class->finalize = xfburn_burn_data_composition_base_dialog_finalize;
  object_class->get_property = xfburn_burn_data_composition_base_dialog_get_property;
  object_class->set_property = xfburn_burn_data_composition_base_dialog_set_property;

  /* properties */
  g_object_class_install_property (object_class, PROP_IMAGE,
				   g_param_spec_pointer ("image", "Image", "Image", G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

static void
xfburn_burn_data_composition_base_dialog_init (XfburnBurnDataCompositionBaseDialog * obj)
{
  XfburnBurnDataCompositionBaseDialogPrivate *priv = XFBURN_BURN_DATA_COMPOSITION_BASE_DIALOG_GET_PRIVATE (obj);
  
  GdkPixbuf *icon = NULL;
  GtkBox *box = GTK_BOX (GTK_DIALOG (obj)->vbox);
  GtkWidget *img;
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *align;
  GtkWidget *button;
  gchar *default_path;
  gchar *tmp_dir;

  gtk_window_set_title (GTK_WINDOW (obj), _("Burn Composition"));
  gtk_window_set_destroy_with_parent (GTK_WINDOW (obj), TRUE);
  icon = gtk_widget_render_icon (GTK_WIDGET (obj), XFBURN_STOCK_BURN_CD, GTK_ICON_SIZE_DIALOG, NULL);
  gtk_window_set_icon (GTK_WINDOW (obj), icon);
  g_object_unref (icon);

  /* burning devices list */
  priv->device_box = xfburn_device_box_new (SHOW_CD_WRITERS | SHOW_CDRW_WRITERS | SHOW_MODE_SELECTION | SHOW_SPEED_SELECTION);
  gtk_widget_show (priv->device_box);

  priv->frame_device = xfce_create_framebox_with_content (_("Burning device"), priv->device_box);
  gtk_widget_show (priv->frame_device);
  gtk_box_pack_start (box, priv->frame_device, FALSE, FALSE, BORDER);

  /* options */
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  frame = xfce_create_framebox_with_content (_("Options"), vbox);
  gtk_widget_show (frame);
  gtk_box_pack_start (box, frame, FALSE, FALSE, BORDER);

  priv->check_eject = gtk_check_button_new_with_mnemonic (_("E_ject disk"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->check_eject), TRUE);
  gtk_widget_show (priv->check_eject);
  gtk_box_pack_start (GTK_BOX (vbox), priv->check_eject, FALSE, FALSE, BORDER);

  priv->check_dummy = gtk_check_button_new_with_mnemonic (_("_Dummy write"));
  gtk_widget_show (priv->check_dummy);
  gtk_box_pack_start (GTK_BOX (vbox), priv->check_dummy, FALSE, FALSE, BORDER);

  priv->check_burnfree = gtk_check_button_new_with_mnemonic (_("Burn_Free"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->check_burnfree), TRUE);
  gtk_widget_show (priv->check_burnfree);
  gtk_box_pack_start (GTK_BOX (vbox), priv->check_burnfree, FALSE, FALSE, BORDER);

  /* create ISO ? */
  priv->check_only_iso = gtk_check_button_new_with_mnemonic (_("Only create _ISO"));
  gtk_widget_show (priv->check_only_iso);
  gtk_box_pack_start (GTK_BOX (vbox), priv->check_only_iso, FALSE, FALSE, BORDER);
  g_signal_connect (G_OBJECT (priv->check_only_iso), "toggled", G_CALLBACK (cb_check_only_iso_toggled), obj);

  align = gtk_alignment_new (0, 0, 0, 0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, BORDER * 4, 0);
  gtk_widget_show (align);
  gtk_box_pack_start (GTK_BOX (vbox), align, FALSE, FALSE, 0);

  priv->hbox_iso = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (priv->hbox_iso);
  gtk_container_add (GTK_CONTAINER (align), priv->hbox_iso);
  gtk_widget_set_sensitive (priv->hbox_iso, FALSE);

  priv->entry_path_iso = gtk_entry_new ();
  tmp_dir = xfburn_settings_get_string ("temporary-dir", g_get_tmp_dir ());
  default_path = g_build_filename (tmp_dir, "xfburn.iso", NULL);
  gtk_entry_set_text (GTK_ENTRY (priv->entry_path_iso), default_path);
  g_free (default_path);
  g_free (tmp_dir);
  gtk_widget_show (priv->entry_path_iso);
  gtk_box_pack_start (GTK_BOX (priv->hbox_iso), priv->entry_path_iso, FALSE, FALSE, 0);

  img = gtk_image_new_from_stock (GTK_STOCK_DIRECTORY, GTK_ICON_SIZE_SMALL_TOOLBAR);
  gtk_widget_show (img);
  button = gtk_button_new ();
  gtk_container_add (GTK_CONTAINER (button), img);
  gtk_widget_show (button);
  gtk_box_pack_start (GTK_BOX (priv->hbox_iso), button, FALSE, FALSE, 0);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (cb_browse_iso), obj);

  /* action buttons */
  button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
  gtk_widget_show (button);
  gtk_dialog_add_action_widget (GTK_DIALOG (obj), button, GTK_RESPONSE_CANCEL);

  button = xfce_create_mixed_button ("xfburn-burn-cd", _("_Burn Composition"));
  gtk_widget_show (button);
  gtk_dialog_add_action_widget (GTK_DIALOG (obj), button, GTK_RESPONSE_OK);
  GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
  gtk_widget_grab_focus (button);
  gtk_widget_grab_default (button);

  g_signal_connect (G_OBJECT (obj), "response", G_CALLBACK (cb_dialog_response), priv);
}

static void
xfburn_burn_data_composition_base_dialog_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  XfburnBurnDataCompositionBaseDialogPrivate *priv = XFBURN_BURN_DATA_COMPOSITION_BASE_DIALOG_GET_PRIVATE (object);

  switch (prop_id) {
  case PROP_IMAGE:
    g_value_set_pointer (value, priv->image);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
xfburn_burn_data_composition_base_dialog_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  XfburnBurnDataCompositionBaseDialogPrivate *priv = XFBURN_BURN_DATA_COMPOSITION_BASE_DIALOG_GET_PRIVATE (object);

  switch (prop_id) {
  case PROP_IMAGE:
    priv->image = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
xfburn_burn_data_composition_base_dialog_finalize (GObject * object)
{
  XfburnBurnDataCompositionBaseDialogPrivate *priv = XFBURN_BURN_DATA_COMPOSITION_BASE_DIALOG_GET_PRIVATE (object);

  iso_image_unref (priv->image);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* internals */
static void
cb_check_only_iso_toggled (GtkToggleButton * button, XfburnBurnDataCompositionBaseDialog * dialog)
{
  XfburnBurnDataCompositionBaseDialogPrivate *priv = XFBURN_BURN_DATA_COMPOSITION_BASE_DIALOG_GET_PRIVATE (dialog);

  gtk_widget_set_sensitive (priv->frame_device, !gtk_toggle_button_get_active (button));
  
  gtk_widget_set_sensitive (priv->hbox_iso, gtk_toggle_button_get_active (button));
  gtk_widget_set_sensitive (priv->check_eject, !gtk_toggle_button_get_active (button));
  gtk_widget_set_sensitive (priv->check_burnfree, !gtk_toggle_button_get_active (button));
  gtk_widget_set_sensitive (priv->check_dummy, !gtk_toggle_button_get_active (button));
}

static void
cb_browse_iso (GtkButton * button, XfburnBurnDataCompositionBaseDialog * dialog)
{
  XfburnBurnDataCompositionBaseDialogPrivate *priv = XFBURN_BURN_DATA_COMPOSITION_BASE_DIALOG_GET_PRIVATE (dialog);
  
  xfburn_browse_for_file (GTK_ENTRY (priv->entry_path_iso), GTK_WINDOW (dialog));
}

typedef struct {
  GtkWidget *dialog_progress;
  struct burn_source *src;
  gchar *iso_path;
} ThreadWriteIsoParams;

static void
thread_write_iso (ThreadWriteIsoParams * params)
{
  GtkWidget *dialog_progress = params->dialog_progress;
  gint fd;
  guchar buf[2048];
  glong size = 0;
  glong written = 0;
  guint i = 0;

  fd = open (params->iso_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd == -1) {
    /* could not create destination */
    gchar err[256];
    gchar *error_msg = NULL;

    strerror_r (errno, err, 256);

    error_msg = g_strdup_printf (_("Could not create destination ISO file: %s"), err);
    xfburn_progress_dialog_burning_failed (XFBURN_PROGRESS_DIALOG (dialog_progress), error_msg);
    g_free (error_msg);

    goto end;
  }

  xfburn_progress_dialog_set_status_with_text (XFBURN_PROGRESS_DIALOG (dialog_progress), XFBURN_PROGRESS_DIALOG_STATUS_RUNNING, _("Writing ISO..."));

  size = (glong) params->src->get_size (params->src);
  /* FIXME: is size really always 2048? */
  while (params->src->read_xt (params->src, buf, 2048) == 2048) {
    if (write (fd, buf, 2048) < 2048) {
      /* an error occured while writing */
      gchar err[256];
      gchar *error_msg = NULL;
      
      strerror_r (errno, err, 256);
    
      error_msg = g_strdup_printf (_("An error occured while writing ISO: %s"), err);
      xfburn_progress_dialog_burning_failed (XFBURN_PROGRESS_DIALOG (dialog_progress), error_msg);
      g_free (error_msg);
      goto cleanup;
    } else {
      written += 2048;
      i++;

      if (i >= 1000) {
	i = 0;
	gdouble percent = 0;

	percent = ((gdouble) written / (gdouble) size);

	xfburn_progress_dialog_set_progress_bar_fraction (XFBURN_PROGRESS_DIALOG (dialog_progress), percent);
      }
    }
  }
  xfburn_progress_dialog_set_status_with_text (XFBURN_PROGRESS_DIALOG (dialog_progress), XFBURN_PROGRESS_DIALOG_STATUS_COMPLETED, _("Done"));

 cleanup:
  close (fd);
 end:
  burn_source_free (params->src);
  g_free (params->iso_path);
  g_free (params);
}

typedef struct {
  GtkWidget *dialog_progress;
  XfburnDevice *device;
  struct burn_source *src;
  gint speed;
  XfburnWriteMode write_mode;
  gboolean eject;
  gboolean dummy;
  gboolean burnfree;
} ThreadBurnCompositionParams;

static void 
thread_burn_prep_and_burn (ThreadBurnCompositionParams * params, struct burn_drive *drive,
                           struct burn_disc *disc, struct burn_session *session, struct burn_track *track)
{
  GtkWidget *dialog_progress = params->dialog_progress;

  struct burn_write_opts * burn_options;
  gint ret;

  ret = burn_disc_add_session (disc, session, BURN_POS_END);
  if (ret == 0) {
    xfburn_progress_dialog_burning_failed (XFBURN_PROGRESS_DIALOG (dialog_progress), _("Unable to create disc object"));
    return;
  }

  if (burn_track_set_source (track, params->src) != BURN_SOURCE_OK) {
    xfburn_progress_dialog_burning_failed (XFBURN_PROGRESS_DIALOG (dialog_progress), _("Cannot attach source object to track object"));
    return;
  }
  
  burn_session_add_track (session, track, BURN_POS_END);

  burn_options = burn_write_opts_new (drive);
  burn_write_opts_set_perform_opc (burn_options, 0);
  burn_write_opts_set_multi (burn_options, 0);

  switch (params->write_mode) {
  case WRITE_MODE_TAO:
    burn_write_opts_set_write_type (burn_options, BURN_WRITE_TAO, BURN_BLOCK_MODE1);
    break;
  case WRITE_MODE_SAO:
    burn_write_opts_set_write_type (burn_options, BURN_WRITE_SAO, BURN_BLOCK_SAO);
    break;
  case WRITE_MODE_RAW16:
    burn_write_opts_set_write_type (burn_options, BURN_WRITE_RAW, BURN_BLOCK_RAW16);
    break;
  case WRITE_MODE_RAW96P:
    burn_write_opts_set_write_type (burn_options, BURN_WRITE_RAW, BURN_BLOCK_RAW96P);
    break;
  case WRITE_MODE_RAW96R:
    burn_write_opts_set_write_type (burn_options, BURN_WRITE_RAW, BURN_BLOCK_RAW96R);
    break;
  default:
    xfburn_progress_dialog_burning_failed (XFBURN_PROGRESS_DIALOG (dialog_progress), _("The write mode is not supported currently"));
    return;
  }

  burn_write_opts_set_simulate(burn_options, params->dummy ? 1 : 0);
  DBG ("TODO set speed");
  burn_drive_set_speed (drive, 0, 0);
  burn_write_opts_set_underrun_proof (burn_options, params->burnfree ? 1 : 0);

  xfburn_perform_burn_write (dialog_progress, drive, params->write_mode, burn_options, disc);

  burn_write_opts_free (burn_options);
}

static void
thread_burn_composition (ThreadBurnCompositionParams * params)
{
  GtkWidget *dialog_progress = params->dialog_progress;

  struct burn_disc *disc;
  struct burn_session *session;
  struct burn_track *track;

  struct burn_drive_info *drive_info = NULL;

  if (!burn_initialize ()) {
    g_critical ("Unable to initialize libburn");
    burn_source_free (params->src);
    g_free (params);
    return;
  }

  disc = burn_disc_create ();
  session = burn_session_create ();
  track = burn_track_create ();

  if (!xfburn_device_grab (params->device, &drive_info)) {
    xfburn_progress_dialog_burning_failed (XFBURN_PROGRESS_DIALOG (dialog_progress), _("Unable to grab drive"));
  } else {
    thread_burn_prep_and_burn (params, drive_info->drive, disc, session, track);
    burn_drive_release (drive_info->drive, params->eject ? 1 : 0);
  }

  burn_track_free (track);
  burn_session_free (session);
  burn_disc_free (disc);
  burn_finish ();

  burn_source_free (params->src);
  g_free (params);
}

static void
cb_dialog_response (XfburnBurnDataCompositionBaseDialog * dialog, gint response_id, XfburnBurnDataCompositionBaseDialogPrivate * priv)
{
  if (response_id == GTK_RESPONSE_OK) {
    GtkWidget *dialog_progress;

    struct burn_source * src = NULL;
    IsoWriteOpts *write_opts;

    /* Sets profile 2 [distribution] */
    iso_write_opts_new (&write_opts, 2);

    if (iso_image_create_burn_source (priv->image, write_opts, &src) < 0) {
      /* could not create source */
      xfce_err (_("Could not create ISO source structure"));
      return;
    }

    /* iso_image_create_burn_source copies the data it needs */
    iso_write_opts_free (write_opts);

    dialog_progress = xfburn_progress_dialog_new (GTK_WINDOW (dialog));
    gtk_window_set_transient_for (GTK_WINDOW (dialog_progress), gtk_window_get_transient_for (GTK_WINDOW (dialog)));
    gtk_widget_hide (GTK_WIDGET (dialog));
    
    gtk_widget_show (dialog_progress);

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->check_only_iso))) {
      ThreadWriteIsoParams *params = NULL;

      /* create a new iso */
      params = g_new0 (ThreadWriteIsoParams, 1);
      params->dialog_progress = dialog_progress;
      params->src = src;
      params->iso_path = g_strdup (gtk_entry_get_text (GTK_ENTRY (priv->entry_path_iso)));
      g_thread_create ((GThreadFunc) thread_write_iso, params, FALSE, NULL);
    }
    else {
      ThreadBurnCompositionParams *params = NULL;
      XfburnDevice *device;
      gint speed;
      XfburnWriteMode write_mode;

      device = xfburn_device_box_get_selected_device (XFBURN_DEVICE_BOX (priv->device_box));
      speed = xfburn_device_box_get_speed (XFBURN_DEVICE_BOX (priv->device_box));
      write_mode = xfburn_device_box_get_mode (XFBURN_DEVICE_BOX (priv->device_box));

      /* burn composition */
      params = g_new0 (ThreadBurnCompositionParams, 1);
      params->dialog_progress = dialog_progress;
      params->device = device;
      params->src = src;
      params->speed = speed;
      params->write_mode = write_mode;
      params->eject = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->check_eject));
      params->dummy = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->check_dummy));
      params->burnfree = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->check_burnfree));
      g_thread_create ((GThreadFunc) thread_burn_composition, params, FALSE, NULL);
    }
  }
}

/* public */
GtkWidget *
xfburn_burn_data_composition_base_dialog_new (IsoImage *image)
{
  XfburnBurnDataCompositionBaseDialog *obj;

  obj = XFBURN_BURN_DATA_COMPOSITION_BASE_DIALOG (g_object_new (XFBURN_TYPE_BURN_DATA_COMPOSITION_BASE_DIALOG, "image", image, NULL));
  
  return GTK_WIDGET (obj);
}