/*
 * hwp-document.c
 *
 * Copyright (C) 2012, Hodong Kim <cogniti@gmail.com>
 * Copyright (C) 2009, Juanjo Marín <juanj.marin@juntadeandalucia.es>
 * Copyright (C) 2004, Red Hat, Inc.
 *
 * hwp-document.c is based on ev-poppler.cc
 * http://git.gnome.org/browse/evince/tree/backend/pdf/ev-poppler.cc
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "config.h"

#include <glib/gi18n-lib.h>
#include <ghwp.h>

#include "hwp-document.h"


struct _HWPDocument
{
    EvDocument parent_instance;

    GHWPDocument *document;
};

struct _HWPDocumentClass
{
    EvDocumentClass parent_class;
};

/* TODO selection, find */

/*static void hwp_document_find_iface_init  (EvDocumentFindInterface  *iface);*/
static void hwp_selection_iface_init      (EvSelectionInterface     *iface);

EV_BACKEND_REGISTER_WITH_CODE (HWPDocument, hwp_document,
    {
/*        EV_BACKEND_IMPLEMENT_INTERFACE (EV_TYPE_DOCUMENT_FIND,*/
/*                                        hwp_document_find_iface_init);*/
        EV_BACKEND_IMPLEMENT_INTERFACE (EV_TYPE_SELECTION,
                                        hwp_selection_iface_init);
    });

static void
hwp_document_init (HWPDocument *hwp_document)
{
}

static void
hwp_document_dispose (GObject *object)
{
    HWPDocument *hwp_document = HWP_DOCUMENT (object);

    if (hwp_document->document) {
        g_object_unref (hwp_document->document);
        hwp_document->document = NULL;
    }

    G_OBJECT_CLASS (hwp_document_parent_class)->dispose (object);
}

static gboolean
hwp_document_load (EvDocument *document,
                   const char *uri,
                   GError    **error)
{
    HWPDocument *hwp_document = HWP_DOCUMENT (document);
    hwp_document->document    = ghwp_document_new_from_uri (uri, error);

    if (!hwp_document->document) {
        return FALSE;
    }

    return TRUE;
}

static gint
hwp_document_get_n_pages (EvDocument  *document)
{
    HWPDocument *hwp_document = HWP_DOCUMENT (document);
    return ghwp_document_get_n_pages (hwp_document->document);
}

static EvPage *
hwp_document_get_page (EvDocument *document,
                       gint        index)
{
    HWPDocument *hwp_document = HWP_DOCUMENT (document);
    GHWPPage    *ghwp_page;
    EvPage      *page;

    ghwp_page = ghwp_document_get_page (hwp_document->document, index);
    page = ev_page_new (index);

    page->backend_page = (EvBackendPage)g_object_ref (ghwp_page);
    page->backend_destroy_func = (EvBackendPageDestroyFunc)g_object_unref;
    g_object_unref (ghwp_page);

    return page;
}

static void
hwp_document_get_page_size (EvDocument *document,
                            EvPage     *page,
                            double     *width,
                            double     *height)
{
    g_return_if_fail (GHWP_IS_PAGE (page->backend_page));
    ghwp_page_get_size (GHWP_PAGE (page->backend_page), width, height);
}

static cairo_surface_t *
hwp_page_render (GHWPPage        *ghwp_page,
		 gint             width,
		 gint             height,
		 EvRenderContext *rc)
{
    cairo_surface_t *surface;
    cairo_t         *cr;
    gdouble          page_width, page_height;
    gdouble          xscale, yscale;

    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                          width, height);
    cr = cairo_create (surface);

    switch (rc->rotation) {
    case 90:
        cairo_translate (cr, width, 0);
        break;
    case 180:
        cairo_translate (cr, width, height);
        break;
    case 270:
        cairo_translate (cr, 0, height);
        break;
    default:
        cairo_translate (cr, 0, 0);
    }

    ghwp_page_get_size (ghwp_page, &page_width, &page_height);
    ev_render_context_compute_scales (rc, page_width, page_height, &xscale, &yscale);

    cairo_scale  (cr, xscale, yscale);
    cairo_rotate (cr, rc->rotation * G_PI / 180.0);
    ghwp_page_render (ghwp_page, cr);

    cairo_set_operator   (cr, CAIRO_OPERATOR_DEST_OVER);
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    cairo_paint          (cr);

    cairo_destroy (cr);

    return surface;
}

static cairo_surface_t *
hwp_document_render (EvDocument      *document,
                     EvRenderContext *rc)
{
    GHWPPage        *ghwp_page;
    gdouble          width_points, height_points;
    gint             width,        height;

    ghwp_page = GHWP_PAGE (rc->page->backend_page);

    ghwp_page_get_size (ghwp_page, &width_points, &height_points);

    ev_render_context_compute_transformed_size (rc, width_points, height_points,
						&width, &height);

    return hwp_page_render(ghwp_page, width, height, rc);
}

static cairo_surface_t *
hwp_document_get_thumbnail_surface (EvDocument      *document,
				    EvRenderContext *rc)
{
    GHWPPage        *ghwp_page;
    cairo_surface_t *surface;
    gdouble          page_width, page_height;
    gint             width, height;

    ghwp_page = GHWP_PAGE (rc->page->backend_page);

    ghwp_page_get_size (ghwp_page, &page_width, &page_height);

    ev_render_context_compute_transformed_size (rc, page_width, page_height,
						&width, &height);

    ev_document_fc_mutex_lock ();
    surface = hwp_page_render (ghwp_page, width, height, rc);
    ev_document_fc_mutex_unlock ();

    return surface;
}

static GdkPixbuf *
hwp_document_get_thumbnail (EvDocument      *document,
			    EvRenderContext *rc)
{
    cairo_surface_t *surface;
    GdkPixbuf       *pixbuf;

    surface = hwp_document_get_thumbnail_surface (document, rc);
    pixbuf = ev_document_misc_pixbuf_from_surface (surface);
    cairo_surface_destroy (surface);

    return pixbuf;
}

static EvDocumentInfo *
hwp_document_get_info (EvDocument *document)
{
    EvDocumentInfo *info;
    GHWPDocument   *ghwp_doc;

    info = g_new0 (EvDocumentInfo, 1);
    info->fields_mask = EV_DOCUMENT_INFO_TITLE         |
                        EV_DOCUMENT_INFO_FORMAT        |
                        EV_DOCUMENT_INFO_SUBJECT       |
                        EV_DOCUMENT_INFO_KEYWORDS      |
                        EV_DOCUMENT_INFO_CREATOR       |
                        EV_DOCUMENT_INFO_CREATION_DATE |
                        EV_DOCUMENT_INFO_MOD_DATE;

    ghwp_doc = HWP_DOCUMENT (document)->document;

    info->title    = ghwp_document_get_title    (ghwp_doc);
    info->format   = ghwp_document_get_format   (ghwp_doc);
    info->subject  = ghwp_document_get_subject  (ghwp_doc);
    info->keywords = ghwp_document_get_keywords (ghwp_doc);
    info->creator  = ghwp_document_get_creator  (ghwp_doc);
    info->creation_date = ghwp_document_get_creation_date     (ghwp_doc);
    info->modified_date = ghwp_document_get_modification_date (ghwp_doc);

    return info;
}

static void
hwp_document_class_init (HWPDocumentClass *klass)
{
    GObjectClass    *gobject_class     = G_OBJECT_CLASS (klass);
    EvDocumentClass *ev_document_class = EV_DOCUMENT_CLASS (klass);
    gobject_class->dispose = hwp_document_dispose;

    ev_document_class->load          = hwp_document_load;
    ev_document_class->get_n_pages   = hwp_document_get_n_pages;
    ev_document_class->get_page      = hwp_document_get_page;
    ev_document_class->get_page_size = hwp_document_get_page_size;
    ev_document_class->render        = hwp_document_render;
    ev_document_class->get_thumbnail = hwp_document_get_thumbnail;
    ev_document_class->get_thumbnail_surface = hwp_document_get_thumbnail_surface;
    /* hwp summary infomation */
    ev_document_class->get_info      = hwp_document_get_info;
}

static void
hwp_selection_render_selection (EvSelection      *selection,
                                EvRenderContext  *rc,
                                cairo_surface_t **surface,
                                EvRectangle      *points,
                                EvRectangle      *old_points,
                                EvSelectionStyle  style,
                                GdkColor         *text,
                                GdkColor         *base)
{
    GHWPPage *ghwp_page;
    cairo_t  *cr;
    GHWPColor text_color, base_color;
    double    width_points, height_points;
    gint      width, height;

    ghwp_page = GHWP_PAGE (rc->page->backend_page);

    ghwp_page_get_size (ghwp_page, &width_points, &height_points);
    width  = (int) ((width_points  * rc->scale) + 0.5);
    height = (int) ((height_points * rc->scale) + 0.5);

    text_color.red   = text->red;
    text_color.green = text->green;
    text_color.blue  = text->blue;

    base_color.red   = base->red;
    base_color.green = base->green;
    base_color.blue  = base->blue;

    if (*surface == NULL) {
        *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                               width, height);
    }

    cr = cairo_create (*surface);
    cairo_scale (cr, rc->scale, rc->scale);
    cairo_surface_set_device_offset (*surface, 0, 0);
    memset (cairo_image_surface_get_data (*surface), 0x00,
        cairo_image_surface_get_height (*surface) *
        cairo_image_surface_get_stride (*surface));
    ghwp_page_render_selection (ghwp_page,
                                cr,
                                (GHWPRectangle *) points,
                                (GHWPRectangle *) old_points,
                                (GHWPSelectionStyle) style,
                                &text_color,
                                &base_color);
    cairo_destroy (cr);
}

static gchar *
hwp_selection_get_selected_text (EvSelection     *selection,
                                 EvPage          *page,
                                 EvSelectionStyle style,
                                 EvRectangle     *points)
{
    g_return_val_if_fail (GHWP_IS_PAGE (page->backend_page), NULL);

    return ghwp_page_get_selected_text (GHWP_PAGE (page->backend_page),
                                        (GHWPSelectionStyle) style,
                                        (GHWPRectangle *) points);
}

static cairo_region_t *
hwp_selection_get_selection_region (EvSelection     *selection,
                                    EvRenderContext *rc,
                                    EvSelectionStyle style,
                                    EvRectangle     *points)
{
    return ghwp_page_get_selection_region (GHWP_PAGE (rc->page->backend_page),
                                           1.0,
                                           (GHWPSelectionStyle) style,
                                           (GHWPRectangle *) points);
}

static void hwp_selection_iface_init (EvSelectionInterface *iface)
{
    iface->render_selection     = hwp_selection_render_selection;
    iface->get_selected_text    = hwp_selection_get_selected_text;
    iface->get_selection_region = hwp_selection_get_selection_region;
}

/*static void
hwp_document_find_iface_init (EvDocumentFindInterface *iface)
{

}*/
