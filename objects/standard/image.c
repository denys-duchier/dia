/* Dia -- an diagram creation/manipulation program
 * Copyright (C) 1998 Alexander Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <config.h>

#include <assert.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#ifdef HAVE_UNIST_H
#include <unistd.h>
#endif
#include <glib/gstdio.h>

#include "intl.h"
#include "message.h"
#include "object.h"
#include "element.h"
#include "connectionpoint.h"
#include "diarenderer.h"
#include "attributes.h"
#include "dia_image.h"
#include "message.h"
#include "properties.h"

#include "tool-icons.h"

#define DEFAULT_WIDTH 2.0
#define DEFAULT_HEIGHT 2.0

#define NUM_CONNECTIONS 9

typedef struct _Image Image;

/*!
 * \brief Standard - Image : rectangular image
 * \extends _Element
 * \ingroup StandardObjects
 */
struct _Image {
  Element element;

  ConnectionPoint connections[NUM_CONNECTIONS];

  real border_width;
  Color border_color;
  LineStyle line_style;
  real dashlength;
  
  DiaImage *image;
  gchar *file;
  
  gboolean inline_data;
  /* may contain the images pixbuf pointer */
  GdkPixbuf *pixbuf;

  gboolean draw_border;
  gboolean keep_aspect;

  time_t mtime;
};

static struct _ImageProperties {
  gchar *file;
  gboolean draw_border;
  gboolean keep_aspect;
} default_properties = { "", FALSE, TRUE };

static real image_distance_from(Image *image, Point *point);
static void image_select(Image *image, Point *clicked_point,
		       DiaRenderer *interactive_renderer);
static ObjectChange* image_move_handle(Image *image, Handle *handle,
				       Point *to, ConnectionPoint *cp,
				       HandleMoveReason reason, ModifierKeys modifiers);
static ObjectChange* image_move(Image *image, Point *to);
static void image_draw(Image *image, DiaRenderer *renderer);
static void image_update_data(Image *image);
static DiaObject *image_create(Point *startpoint,
			  void *user_data,
			  Handle **handle1,
			  Handle **handle2);
static void image_destroy(Image *image);
static DiaObject *image_copy(Image *image);

static PropDescription *image_describe_props(Image *image);
static void image_get_props(Image *image, GPtrArray *props);
static void image_set_props(Image *image, GPtrArray *props);

static void image_save(Image *image, ObjectNode obj_node, const char *filename);
static DiaObject *image_load(ObjectNode obj_node, int version, DiaContext *ctx);

static ObjectTypeOps image_type_ops =
{
  (CreateFunc) image_create,
  (LoadFunc)   image_load,
  (SaveFunc)   image_save,
  (GetDefaultsFunc)   NULL,
  (ApplyDefaultsFunc) NULL
};

static PropDescription image_props[] = {
  ELEMENT_COMMON_PROPERTIES,
  { "image_file", PROP_TYPE_FILE, PROP_FLAG_VISIBLE,
    N_("Image file"), NULL, NULL},
  { "inline_data", PROP_TYPE_BOOL, PROP_FLAG_DONT_MERGE|PROP_FLAG_VISIBLE|PROP_FLAG_OPTIONAL,
    N_("Inline data"), N_("Store image data in diagram"), NULL },
  { "pixbuf", PROP_TYPE_PIXBUF, PROP_FLAG_OPTIONAL,
    N_("Pixbuf"), N_("The Pixbuf reference"), NULL },
  { "show_border", PROP_TYPE_BOOL, PROP_FLAG_VISIBLE,
    N_("Draw border"), NULL, NULL},
  { "keep_aspect", PROP_TYPE_BOOL, PROP_FLAG_VISIBLE,
    N_("Keep aspect ratio"), NULL, NULL},
  PROP_STD_LINE_WIDTH,
  PROP_STD_LINE_COLOUR,
  PROP_STD_LINE_STYLE,
  PROP_DESC_END
};

DiaObjectType image_type =
{
  "Standard - Image",  /* name */
  0,                 /* version */
  (char **) image_icon, /* pixmap */

  &image_type_ops,      /* ops */
  NULL,
  0,
  image_props
};

DiaObjectType *_image_type = (DiaObjectType *) &image_type;

static ObjectOps image_ops = {
  (DestroyFunc)         image_destroy,
  (DrawFunc)            image_draw,
  (DistanceFunc)        image_distance_from,
  (SelectFunc)          image_select,
  (CopyFunc)            image_copy,
  (MoveFunc)            image_move,
  (MoveHandleFunc)      image_move_handle,
  (GetPropertiesFunc)   object_create_props_dialog,
  (ApplyPropertiesDialogFunc) object_apply_props_from_dialog,
  (ObjectMenuFunc)      NULL,
  (DescribePropsFunc)   object_describe_props,
  (GetPropsFunc)        image_get_props,
  (SetPropsFunc)        image_set_props,
  (TextEditFunc) 0,
  (ApplyPropertiesListFunc) object_apply_props,
};

static PropOffset image_offsets[] = {
  ELEMENT_COMMON_PROPERTIES_OFFSETS,
  { "image_file", PROP_TYPE_FILE, offsetof(Image, file) },
  { "inline_data", PROP_TYPE_BOOL, offsetof(Image, inline_data) },
  { "pixbuf", PROP_TYPE_PIXBUF, offsetof(Image, pixbuf) },
  { "show_border", PROP_TYPE_BOOL, offsetof(Image, draw_border) },
  { "keep_aspect", PROP_TYPE_BOOL, offsetof(Image, keep_aspect) },
  { PROP_STDNAME_LINE_WIDTH, PROP_STDTYPE_LINE_WIDTH, offsetof(Image, border_width) },
  { "line_colour", PROP_TYPE_COLOUR, offsetof(Image, border_color) },
  { "line_style", PROP_TYPE_LINESTYLE,
    offsetof(Image, line_style), offsetof(Image, dashlength) },
  { NULL, 0, 0 }
};

/*!
 * \brief Get properties of the _Image
 * \memberof _Image
 * Overwites DiaObject::get_props() to initialize pixbuf property
 */
static void
image_get_props(Image *image, GPtrArray *props)
{
  if (image->inline_data)
    image->pixbuf = (GdkPixbuf *)dia_image_pixbuf (image->image);
  object_get_props_from_offsets(&image->element.object, image_offsets, props);
}

static void
image_set_props(Image *image, GPtrArray *props)
{
  struct stat st;
  time_t mtime = 0;
  char *old_file = image->file ? g_strdup(image->file) : NULL;
  const GdkPixbuf *old_pixbuf = dia_image_pixbuf (image->image);
  gboolean was_inline = image->inline_data;

  object_set_props_from_offsets(&image->element.object, image_offsets, props);

  if (old_pixbuf != image->pixbuf) {
    if (!image->file || *image->file == '\0') {
      GdkPixbuf *pixbuf = NULL;
      image->inline_data = TRUE; /* otherwise we'll loose it */
      /* somebody deleting the filename? */
      if (!image->pixbuf && image->image)
	pixbuf = g_object_ref ((GdkPixbuf *)dia_image_pixbuf (image->image));
      if (image->image)
        g_object_unref (image->image);
      image->image = dia_image_new_from_pixbuf (image->pixbuf ? image->pixbuf : pixbuf);
      image->pixbuf = (GdkPixbuf *)dia_image_pixbuf (image->image);
      if (pixbuf)
	g_object_unref (pixbuf);
    } else {
      if (image->pixbuf)
        message_warning ("FIXME: handle pixbuf change!");
    }
  }

  /* use old value on error */
  if (!image->file || g_stat (image->file, &st) != 0)
    mtime = image->mtime;
  else
    mtime = st.st_mtime;

  /* handle changing the image. */
  if (image->file && image->pixbuf && was_inline && !image->inline_data) {
    /* export inline data */
    if (was_inline && !image->inline_data)
      /* if saving fails we keep it inline */
      image->inline_data = !dia_image_save (image->image, image->file);
  } else if (image->file && ((old_file && strcmp(image->file, old_file) != 0) || image->mtime != mtime)) {
    Element *elem = &image->element;
    DiaImage *img = NULL;

    if ((img = dia_image_load(image->file)) != NULL)
      image->image = img;
    else if (!image->pixbuf) /* dont overwrite inlined */
      image->image = dia_image_get_broken();
    elem->height = (elem->width*(float)dia_image_height(image->image))/
      (float)dia_image_width(image->image);
  }
  g_free(old_file);
  /* remember it */
  image->mtime = mtime;

  image_update_data(image);
}

static real
image_distance_from(Image *image, Point *point)
{
  Element *elem = &image->element;
  Rectangle rect;

  rect.left = elem->corner.x - image->border_width;
  rect.right = elem->corner.x + elem->width + image->border_width;
  rect.top = elem->corner.y - image->border_width;
  rect.bottom = elem->corner.y + elem->height + image->border_width;
  return distance_rectangle_point(&rect, point);
}

static void
image_select(Image *image, Point *clicked_point,
	     DiaRenderer *interactive_renderer)
{
  element_update_handles(&image->element);
}

static ObjectChange*
image_move_handle(Image *image, Handle *handle,
		  Point *to, ConnectionPoint *cp,
		  HandleMoveReason reason, ModifierKeys modifiers)
{
  Element *elem = &image->element;
  assert(image!=NULL);
  assert(handle!=NULL);
  assert(to!=NULL);

  if (image->keep_aspect) {
    float width, height;
    float new_width, new_height;
    width = image->element.width;
    height = image->element.height;

    switch (handle->id) {
    case HANDLE_RESIZE_NW:
      new_width = -(to->x-image->element.corner.x)+width;
      new_height = -(to->y-image->element.corner.y)+height;
      if (new_height == 0 || new_width/new_height > width/height) {
	new_height = new_width*height/width;
      } else {
	new_width = new_height*width/height;
      }
      to->x = image->element.corner.x+(image->element.width-new_width);
      to->y = image->element.corner.y+(image->element.height-new_height);
      element_move_handle(elem, HANDLE_RESIZE_NW, to, cp, reason, modifiers);
      break;
    case HANDLE_RESIZE_N:
      new_width = (-(to->y-image->element.corner.y)+height)*width/height;
      to->x = image->element.corner.x+new_width;
      element_move_handle(elem, HANDLE_RESIZE_NE, to, cp, reason, modifiers);
      break;
    case HANDLE_RESIZE_NE:
      new_width = to->x-image->element.corner.x;
      new_height = -(to->y-image->element.corner.y)+height;
      if (new_height == 0 || new_width/new_height > width/height) {
	new_height = new_width*height/width;
      } else {
	new_width = new_height*width/height;
      }
      to->x = image->element.corner.x+new_width;
      to->y = image->element.corner.y+(image->element.height-new_height);
      element_move_handle(elem, HANDLE_RESIZE_NE, to, cp, reason, modifiers);
      break;
    case HANDLE_RESIZE_E:
      new_height = (to->x-image->element.corner.x)*height/width;
      to->y = image->element.corner.y+new_height;
      element_move_handle(elem, HANDLE_RESIZE_SE, to, cp, reason, modifiers);
      break;
    case HANDLE_RESIZE_SE:
      new_width = to->x-image->element.corner.x;
      new_height = to->y-image->element.corner.y;
      if (new_height == 0 || new_width/new_height > width/height) {
	new_height = new_width*height/width;
      } else {
	new_width = new_height*width/height;
      }
      to->x = image->element.corner.x+new_width;
      to->y = image->element.corner.y+new_height;
      element_move_handle(elem, HANDLE_RESIZE_SE, to, cp, reason, modifiers);
      break;
    case HANDLE_RESIZE_S:
      new_width = (to->y-image->element.corner.y)*width/height;
      to->x = image->element.corner.x+new_width;
      element_move_handle(elem, HANDLE_RESIZE_SE, to, cp, reason, modifiers);
      break;
    case HANDLE_RESIZE_SW:
      new_width = -(to->x-image->element.corner.x)+width;
      new_height = to->y-image->element.corner.y;
      if (new_height == 0 || new_width/new_height > width/height) {
	new_height = new_width*height/width;
      } else {
	new_width = new_height*width/height;
      }
      to->x = image->element.corner.x+(image->element.width-new_width);
      to->y = image->element.corner.y+new_height;
      element_move_handle(elem, HANDLE_RESIZE_SW, to, cp, reason, modifiers);
      break;
    case HANDLE_RESIZE_W:
      new_height = (-(to->x-image->element.corner.x)+width)*height/width;
      to->y = image->element.corner.y+new_height;
      element_move_handle(elem, HANDLE_RESIZE_SW, to, cp, reason, modifiers);
      break;
    default:
      message_warning("Unforeseen handle in image_move_handle: %d\n",
		      handle->id);
      
    }
  } else {
    element_move_handle(elem, handle->id, to, cp, reason, modifiers);
  }
  image_update_data(image);

  return NULL;
}

static ObjectChange*
image_move(Image *image, Point *to)
{
  image->element.corner = *to;
  
  image_update_data(image);

  return NULL;
}

static void
image_draw(Image *image, DiaRenderer *renderer)
{
  DiaRendererClass *renderer_ops = DIA_RENDERER_GET_CLASS (renderer);
  Point ul_corner, lr_corner;
  Element *elem;
  
  assert(image != NULL);
  assert(renderer != NULL);

  elem = &image->element;
  
  lr_corner.x = elem->corner.x + elem->width + image->border_width/2;
  lr_corner.y = elem->corner.y + elem->height + image->border_width/2;
  
  ul_corner.x = elem->corner.x - image->border_width/2;
  ul_corner.y = elem->corner.y - image->border_width/2;

  if (image->draw_border) {
    renderer_ops->set_linewidth(renderer, image->border_width);
    renderer_ops->set_linestyle(renderer, image->line_style);
    renderer_ops->set_dashlength(renderer, image->dashlength);
    renderer_ops->set_linejoin(renderer, LINEJOIN_MITER);
    
    renderer_ops->draw_rect(renderer, 
			     &ul_corner,
			     &lr_corner, 
			     &image->border_color);
  }
  /* Draw the image */
  if (image->image) {
    renderer_ops->draw_image(renderer, &elem->corner, elem->width,
			      elem->height, image->image);
  } else {
    DiaImage *broken = dia_image_get_broken();
    renderer_ops->draw_image(renderer, &elem->corner, elem->width,
			      elem->height, broken);
    dia_image_unref(broken);
  }
}

static void
image_update_data(Image *image)
{
  Element *elem = &image->element;
  ElementBBExtras *extra = &elem->extra_spacing;
  DiaObject *obj = &elem->object;

  if (image->keep_aspect && image->image) {
    /* maybe the image got changes since */
    real aspect_org = (float)dia_image_width(image->image)
                    / (float)dia_image_height(image->image);
    real aspect_now = elem->width / elem->height;

    if (fabs (aspect_now - aspect_org) > 1e-4) {
      elem->height = elem->width /aspect_org;
    }
  }

  /* Update connections: */
  image->connections[0].pos = elem->corner;
  image->connections[1].pos.x = elem->corner.x + elem->width / 2.0;
  image->connections[1].pos.y = elem->corner.y;
  image->connections[2].pos.x = elem->corner.x + elem->width;
  image->connections[2].pos.y = elem->corner.y;
  image->connections[3].pos.x = elem->corner.x;
  image->connections[3].pos.y = elem->corner.y + elem->height / 2.0;
  image->connections[4].pos.x = elem->corner.x + elem->width;
  image->connections[4].pos.y = elem->corner.y + elem->height / 2.0;
  image->connections[5].pos.x = elem->corner.x;
  image->connections[5].pos.y = elem->corner.y + elem->height;
  image->connections[6].pos.x = elem->corner.x + elem->width / 2.0;
  image->connections[6].pos.y = elem->corner.y + elem->height;
  image->connections[7].pos.x = elem->corner.x + elem->width;
  image->connections[7].pos.y = elem->corner.y + elem->height;
  image->connections[8].pos.x = elem->corner.x + elem->width / 2.0;
  image->connections[8].pos.y = elem->corner.y + elem->height / 2.0;
  
  extra->border_trans = (image->draw_border ? image->border_width / 2.0 : 0.0);
  element_update_boundingbox(elem);
  
  obj->position = elem->corner;
  image->connections[8].directions = DIR_ALL;
  element_update_handles(elem);
}


static DiaObject *
image_create(Point *startpoint,
	     void *user_data,
	     Handle **handle1,
	     Handle **handle2)
{
  Image *image;
  Element *elem;
  DiaObject *obj;
  int i;

  image = g_malloc0(sizeof(Image));
  elem = &image->element;
  obj = &elem->object;
  
  obj->type = &image_type;
  obj->ops = &image_ops;

  elem->corner = *startpoint;
  elem->width = DEFAULT_WIDTH;
  elem->height = DEFAULT_HEIGHT;
    
  image->border_width =  attributes_get_default_linewidth();
  image->border_color = attributes_get_foreground();
  attributes_get_default_line_style(&image->line_style,
				    &image->dashlength);
  
  element_init(elem, 8, NUM_CONNECTIONS);

  for (i=0; i<NUM_CONNECTIONS; i++) {
    obj->connections[i] = &image->connections[i];
    image->connections[i].object = obj;
    image->connections[i].connected = NULL;
  }
  image->connections[8].flags = CP_FLAGS_MAIN;

  if (strcmp(default_properties.file, "")) {
    image->file = g_strdup(default_properties.file);
    image->image = dia_image_load(image->file);

    if (image->image) {
      elem->width = (elem->width*(float)dia_image_width(image->image))/
	(float)dia_image_height(image->image);
    }
  } else {
    image->file = g_strdup("");
    image->image = NULL;
  }

  image->draw_border = default_properties.draw_border;
  image->keep_aspect = default_properties.keep_aspect;

  image_update_data(image);
  
  *handle1 = NULL;
  *handle2 = obj->handles[7];  
  return &image->element.object;
}

static void 
image_destroy(Image *image) {
  if (image->file != NULL)
    g_free(image->file);

  if (image->image != NULL)
    dia_image_unref(image->image);

  element_destroy(&image->element);
}

static DiaObject *
image_copy(Image *image)
{
  int i;
  Image *newimage;
  Element *elem, *newelem;
  DiaObject *newobj;
  
  elem = &image->element;
  
  newimage = g_malloc0(sizeof(Image));
  newelem = &newimage->element;
  newobj = &newelem->object;

  element_copy(elem, newelem);

  newimage->border_width = image->border_width;
  newimage->border_color = image->border_color;
  newimage->line_style = image->line_style;
  
  for (i=0;i<NUM_CONNECTIONS;i++) {
    newobj->connections[i] = &newimage->connections[i];
    newimage->connections[i].object = newobj;
    newimage->connections[i].connected = NULL;
    newimage->connections[i].pos = image->connections[i].pos;
    newimage->connections[i].last_pos = image->connections[i].last_pos;
    newimage->connections[i].flags = image->connections[i].flags;
  }

  newimage->file = g_strdup(image->file);
  if (image->image)
    dia_image_add_ref(image->image);
  newimage->image = image->image;

  /* not sure if we only want a reference, but it would be safe when 
   * someday editing doe not work inplace, but creates new pixbufs 
   * for every single undoable step */
  newimage->inline_data = image->inline_data;
  if (image->pixbuf)
    newimage->pixbuf = g_object_ref(image->pixbuf);
  else
    newimage->pixbuf = image->pixbuf;

  newimage->mtime = image->mtime;
  newimage->draw_border = image->draw_border;
  newimage->keep_aspect = image->keep_aspect;

  return &newimage->element.object;
}

/* Gets the directory path of a filename.
   Uses current working directory if filename is a relative pathname.
   Examples:
     /some/dir/file.gif => /some/dir
     dir/file.gif => /cwd/dir
   
*/
static char *
get_directory(const char *filename) 
{
  char *cwd;
  char *directory;
  char *dirname;
  
  if (filename==NULL)
    return NULL;

  dirname = g_path_get_dirname(filename);
  if (g_path_is_absolute(dirname)) {
      directory = g_build_path(G_DIR_SEPARATOR_S, dirname, NULL);
  } else {
      cwd = g_get_current_dir();
      directory = g_build_path(G_DIR_SEPARATOR_S, cwd, dirname, NULL);
      g_free(cwd);
  }
  g_free(dirname);

  return directory;
}

static void
image_save(Image *image, ObjectNode obj_node, const char *filename)
{
  char *diafile_dir;
  
  element_save(&image->element, obj_node);

  if (image->border_width != 0.1)
    data_add_real(new_attribute(obj_node, "border_width"),
		  image->border_width);
  
  if (!color_equals(&image->border_color, &color_black))
    data_add_color(new_attribute(obj_node, "border_color"),
		   &image->border_color);
  
  if (image->line_style != LINESTYLE_SOLID)
    data_add_enum(new_attribute(obj_node, "line_style"),
		  image->line_style);

  if (image->line_style != LINESTYLE_SOLID &&
      image->dashlength != DEFAULT_LINESTYLE_DASHLEN)
    data_add_real(new_attribute(obj_node, "dashlength"),
		  image->dashlength);
  
  data_add_boolean(new_attribute(obj_node, "draw_border"), image->draw_border);
  data_add_boolean(new_attribute(obj_node, "keep_aspect"), image->keep_aspect);

  if (image->file != NULL) {
    if (g_path_is_absolute(image->file)) { /* Absolute pathname */
      diafile_dir = get_directory(filename);

      if (strncmp(diafile_dir, image->file, strlen(diafile_dir))==0) {
	/* The image pathname has the dia file pathname in the begining */
	/* Save the relative path: */
	data_add_filename(new_attribute(obj_node, "file"), image->file + strlen(diafile_dir) + 1);
      } else {
	/* Save the absolute path: */
	data_add_filename(new_attribute(obj_node, "file"), image->file);
      }
      
      g_free(diafile_dir);
      
    } else {
      /* Relative path. Must be an erronous filename...
	 Just save the filename. */
      data_add_filename(new_attribute(obj_node, "file"), image->file);
    }
    
  }
  /* only save image_data inline if told to do so */
  if (image->inline_data) {
    GdkPixbuf *pixbuf;
    data_add_boolean (new_attribute(obj_node, "inline_data"), image->inline_data);

    /* just to be sure to get the currently visible */
    pixbuf = (GdkPixbuf *)dia_image_pixbuf (image->image);
    if (pixbuf != image->pixbuf && image->pixbuf != NULL)
      message_warning (_("Inconsistent pixbuf during image save."));
    if (pixbuf)
      data_add_pixbuf (new_attribute(obj_node, "pixbuf"), pixbuf);
  }
}

static DiaObject *
image_load(ObjectNode obj_node, int version, DiaContext *ctx)
{
  Image *image;
  Element *elem;
  DiaObject *obj;
  int i;
  AttributeNode attr;
  char *diafile_dir;
  
  image = g_malloc0(sizeof(Image));
  elem = &image->element;
  obj = &elem->object;
  
  obj->type = &image_type;
  obj->ops = &image_ops;

  element_load(elem, obj_node, ctx);
  
  image->border_width = 0.1;
  attr = object_find_attribute(obj_node, "border_width");
  if (attr != NULL)
    image->border_width =  data_real(attribute_first_data(attr), ctx);

  image->border_color = color_black;
  attr = object_find_attribute(obj_node, "border_color");
  if (attr != NULL)
    data_color(attribute_first_data(attr), &image->border_color, ctx);
  
  image->line_style = LINESTYLE_SOLID;
  attr = object_find_attribute(obj_node, "line_style");
  if (attr != NULL)
    image->line_style =  data_enum(attribute_first_data(attr), ctx);

  image->dashlength = DEFAULT_LINESTYLE_DASHLEN;
  attr = object_find_attribute(obj_node, "dashlength");
  if (attr != NULL)
    image->dashlength = data_real(attribute_first_data(attr), ctx);

  image->draw_border = TRUE;
  attr = object_find_attribute(obj_node, "draw_border");
  if (attr != NULL)
    image->draw_border =  data_boolean(attribute_first_data(attr), ctx);

  image->keep_aspect = TRUE;
  attr = object_find_attribute(obj_node, "keep_aspect");
  if (attr != NULL)
    image->keep_aspect =  data_boolean(attribute_first_data(attr), ctx);

  attr = object_find_attribute(obj_node, "file");
  if (attr != NULL) {
    image->file =  data_filename(attribute_first_data(attr), ctx);
  } else {
    image->file = g_strdup("");
  }

  element_init(elem, 8, NUM_CONNECTIONS);

  for (i=0;i<NUM_CONNECTIONS;i++) {
    obj->connections[i] = &image->connections[i];
    image->connections[i].object = obj;
    image->connections[i].connected = NULL;
  }
  image->connections[8].flags = CP_FLAGS_MAIN;

  image->image = NULL;
  
  if (strcmp(image->file, "")!=0) {
    diafile_dir = get_directory(dia_context_get_filename(ctx));

    if (g_path_is_absolute(image->file)) { /* Absolute pathname */
      image->image = dia_image_load(image->file);
      if (image->image == NULL) {
	/* Not found as abs path, try in same dir as diagram. */
	char *temp_string;
	const char *image_file_name = image->file;
	const char *psep;

	psep = strrchr(image->file, G_DIR_SEPARATOR);
	/* try the other G_OS as well */
	if (!psep)
	  psep =  strrchr(image->file, G_DIR_SEPARATOR == '/' ? '\\' : '/');
	if (psep)
	  image_file_name = psep + 1;

	temp_string = g_build_filename(diafile_dir, image_file_name, NULL);

	image->image = dia_image_load(temp_string);

	if (image->image != NULL) {
	  /* Found file in same dir as diagram. */
	  message_warning(_("The image file '%s' was not found in the specified directory.\n"
			  "Using the file '%s' instead.\n"), image->file, temp_string);
	  g_free(image->file);
	  image->file = temp_string;
	} else {
	  g_free(temp_string);
	  
	  image->image = dia_image_load((char *)image_file_name);
	  if (image->image != NULL) {
	    char *tmp;
	    /* Found file in current dir. */
	    message_warning(_("The image file '%s' was not found in the specified directory.\n"
			    "Using the file '%s' instead.\n"), image->file, image_file_name);
	    tmp = image->file;
	    image->file = g_strdup(image_file_name);
	    g_free(tmp);
	  } else {
	    message_warning(_("The image file '%s' was not found.\n"),
			    image_file_name);
	  }
	}
      }
    } else { /* Relative pathname: */
      char *temp_string;

      temp_string = g_build_filename (diafile_dir, image->file, NULL);

      image->image = dia_image_load(temp_string);

      if (image->image != NULL) {
	/* Found file in same dir as diagram. */
	g_free(image->file);
	image->file = temp_string;
      } else {
	g_free(temp_string);
	  
	image->image = dia_image_load(image->file);
	if (image->image == NULL) {
	  /* Didn't find file in current dir. */
	  message_warning(_("The image file '%s' was not found.\n"),
			  image->file);
	}
      }
    }
    g_free(diafile_dir);
  }
  /* if we don't have an image yet try to recover it from inlined data */
  if (!image->image) {
    attr = object_find_attribute(obj_node, "pixbuf");
    if (attr != NULL) {
      GdkPixbuf *pixbuf = data_pixbuf (attribute_first_data(attr));

      if (pixbuf) {
	image->image = dia_image_new_from_pixbuf (pixbuf);
	image->inline_data = TRUE; /* avoid loosing it */
	/* FIXME: should we reset the filename? */
	g_object_unref (pixbuf);
      }
    }
  }

  /* update mtime */
  {
    struct stat st;
    if (g_stat (image->file, &st) != 0)
      st.st_mtime = 0;

    image->mtime = st.st_mtime;
  }
  image_update_data(image);

  return &image->element.object;
}
