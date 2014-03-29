/**
 * Functions for dealing with window borders.
 * Copyright (C) 2004 Joe Wingbermuehle
 * 
 */

#include "jwm.h"
#include "border.h"
#include "client.h"
#include "clientlist.h"
#include "color.h"
#include "main.h"
#include "icon.h"
#include "font.h"
#include "error.h"
#include "misc.h"
#include "settings.h"
#include "grab.h"
#include "button.h"

static GC borderGC;
static char *buttonNames[BI_COUNT];
static IconNode *buttonIcons[BI_COUNT];

static void DrawBorderHelper(const ClientNode *np);
static void DrawBorderHandles(const ClientNode *np);
static void DrawBorderButtons(const ClientNode *np, Pixmap canvas);
static char DrawBorderIcon(BorderIconType t,
                           unsigned int xoffset, unsigned int yoffset,
                           Pixmap canvas);
static void DrawCloseButton(unsigned int xoffset, unsigned int yoffset,
                            Pixmap canvas);
static void DrawMaxIButton(unsigned int xoffset, unsigned int yoffset,
                           Pixmap canvas);
static void DrawMaxAButton(unsigned int xoffset, unsigned int yoffset,
                           Pixmap canvas);
static void DrawMinButton(unsigned int xoffset, unsigned int yoffset,
                          Pixmap canvas);
static unsigned int GetButtonCount(const ClientNode *np);

#ifdef USE_SHAPE
static void FillRoundedRectangle(Drawable d, GC gc, int x, int y,
                                 int width, int height, int radius);
#endif

/** Initialize structures. */
void InitializeBorders(void)
{
   memset(buttonNames, 0, sizeof(buttonNames));
}

/** Initialize server resources. */
void StartupBorders(void)
{

   XGCValues gcValues;
   unsigned long gcMask;
   unsigned int i;

   gcMask = GCGraphicsExposures;
   gcValues.graphics_exposures = False;
   borderGC = JXCreateGC(display, rootWindow, gcMask, &gcValues);

   for(i = 0; i < BI_COUNT; i++) {
      if(buttonNames[i]) {
         buttonIcons[i] = LoadNamedIcon(buttonNames[i], 1);
         Release(buttonNames[i]);
      } else {
         buttonIcons[i] = NULL;
      }
   }

}

/** Release server resources. */
void ShutdownBorders(void)
{
   JXFreeGC(display, borderGC);
}

/** Get the size of the icon to display on a window. */
int GetBorderIconSize(void)
{
   return settings.titleHeight - 4;
}

/** Determine the border action to take given coordinates. */
BorderActionType GetBorderActionType(const ClientNode *np, int x, int y)
{

   int north, south, east, west;
   unsigned int resizeMask;
   unsigned int titleHeight = settings.titleHeight;
   if(settings.handles && !(np->state.status & STAT_VMAX)) {
      titleHeight += settings.borderWidth;
   }

   GetBorderSize(&np->state, &north, &south, &east, &west);

   /* Check title bar actions. */
   if((np->state.border & BORDER_TITLE) &&
      titleHeight > settings.borderWidth) {

      /* Check buttons on the title bar. */
      int offset = np->width + west;
      if(y >= settings.borderWidth && y <= titleHeight) {

         /* Menu button. */
         if(np->width >= titleHeight) {
            if(x > settings.borderWidth && x <= titleHeight) {
               return BA_MENU;
            }
         }

         /* Close button. */
         if((np->state.border & BORDER_CLOSE) && offset > 2 * titleHeight) {
            if(x > offset - titleHeight && x < offset) {
               return BA_CLOSE;
            }
            offset -= titleHeight;
         }

         /* Maximize button. */
         if((np->state.border & BORDER_MAX) && offset > 2 * titleHeight) {
            if(x > offset - titleHeight && x < offset) {
               return BA_MAXIMIZE;
            }
            offset -= titleHeight;
         }

         /* Minimize button. */
         if((np->state.border & BORDER_MIN) && offset > 2 * titleHeight) {
            if(x > offset - titleHeight && x < offset) {
               return BA_MINIMIZE;
            }
         }

      }

      /* Check for move. */
      if(y >= settings.borderWidth && y <= titleHeight) {
         if(x > settings.borderWidth && x < offset) {
            if(np->state.border & BORDER_MOVE) {
               return BA_MOVE;
            } else {
               return BA_NONE;
            }
         }
      }

   }

   /* Now we check resize actions.
    * There is no need to go further if resizing isn't allowed. */
   if(!(np->state.border & BORDER_RESIZE)) {
      return BA_NONE;
   }

   /* We don't allow resizing maximized windows. */
   resizeMask = BA_RESIZE_S | BA_RESIZE_N
              | BA_RESIZE_E | BA_RESIZE_W
              | BA_RESIZE;
   if(np->state.status & STAT_HMAX) {
      resizeMask &= ~(BA_RESIZE_E | BA_RESIZE_W);
   }
   if(np->state.status & STAT_VMAX) {
      resizeMask &= ~(BA_RESIZE_N | BA_RESIZE_S);
   }
   if(np->state.status & STAT_SHADED) {
      resizeMask &= ~(BA_RESIZE_N | BA_RESIZE_S);
   }

   /* Check south east/west and north east/west resizing. */
   if(   np->width >= settings.titleHeight * 2
      && np->height >= settings.titleHeight * 2) {
      if(y > np->height + north - settings.titleHeight) {
         if(x < settings.titleHeight) {
            return (BA_RESIZE_S | BA_RESIZE_W | BA_RESIZE) & resizeMask;
         } else if(x > np->width + west - settings.titleHeight) {
            return (BA_RESIZE_S | BA_RESIZE_E | BA_RESIZE) & resizeMask;
         }
      } else if(y < settings.titleHeight) {
         if(x < settings.titleHeight) {
            return (BA_RESIZE_N | BA_RESIZE_W | BA_RESIZE) & resizeMask;
         } else if(x > np->width + west - settings.titleHeight) {
            return (BA_RESIZE_N | BA_RESIZE_E | BA_RESIZE) & resizeMask;
         }
      }
   }

   /* Check east, west, north, and south resizing. */
   if(x <= west) {
      return (BA_RESIZE_W | BA_RESIZE) & resizeMask;
   } else if(x >= np->width + west) {
      return (BA_RESIZE_E | BA_RESIZE) & resizeMask;
   } else if(y >= np->height + north) {
      return (BA_RESIZE_S | BA_RESIZE) & resizeMask;
   } else if(y <= south) {
      return (BA_RESIZE_N | BA_RESIZE) & resizeMask;
   } else {
      return BA_NONE;
   }

}

/** Reset the shape of a window border. */
void ResetBorder(const ClientNode *np)
{
#ifdef USE_SHAPE
   Pixmap shapePixmap;
   GC shapeGC;
#endif

   int north, south, east, west;
   int width, height;

   GrabServer();

   /* Determine the size of the window. */
   GetBorderSize(&np->state, &north, &south, &east, &west);
   width = np->width + east + west;
   if(np->state.status & STAT_SHADED) {
      height = north + south;
   } else {
      height = np->height + north + south;
   }

   /** Set the window size. */
   if(!(np->state.status & STAT_SHADED)) {
      JXMoveResizeWindow(display, np->window, west, north,
                         np->width, np->height);
   }
   JXMoveResizeWindow(display, np->parent, np->x - west, np->y - north,
                      width, height);

#ifdef USE_SHAPE

   /* First set the shape to the window border. */
   shapePixmap = JXCreatePixmap(display, np->parent, width, height, 1);
   shapeGC = JXCreateGC(display, shapePixmap, 0, NULL);

   if(settings.borderRadius > 1) {

      /* Make the whole area transparent. */
      JXSetForeground(display, shapeGC, 0);
      JXFillRectangle(display, shapePixmap, shapeGC, 0, 0, width, height);

      /* Draw the window area without the corners. */
      /* Corner bound radius -1 to allow slightly better outline drawing */
      JXSetForeground(display, shapeGC, 1);
      if((np->state.status & (STAT_HMAX | STAT_VMAX)) &&
         !(np->state.status & (STAT_SHADED))) {
         JXFillRectangle(display, shapePixmap, shapeGC, 0, 0, width, height);
      } else {
         FillRoundedRectangle(shapePixmap, shapeGC, 0, 0, width, height,
                              settings.borderRadius - 1);
      }

   } else {

      JXSetForeground(display, shapeGC, 1);
      JXFillRectangle(display, shapePixmap, shapeGC, 0, 0, width, height);

   }

   /* Apply the client window. */
   if(!(np->state.status & STAT_SHADED) && (np->state.status & STAT_SHAPED)) {

      XRectangle *rects;
      int count;
      int ordering;

      /* Cut out an area for the client window. */
      JXSetForeground(display, shapeGC, 0);
      JXFillRectangle(display, shapePixmap, shapeGC, west, north,
                      np->width, np->height);

      /* Fill in the visible area. */
      rects = JXShapeGetRectangles(display, np->window, ShapeBounding,
                                   &count, &ordering);
      if(JLIKELY(rects)) {
         int i;
         for(i = 0; i < count; i++) {
            rects[i].x += east;
            rects[i].y += north;
         }
         JXSetForeground(display, shapeGC, 1);
         JXFillRectangles(display, shapePixmap, shapeGC, rects, count);
         JXFree(rects);
      }

   }

   /* Set the shape. */
   JXShapeCombineMask(display, np->parent, ShapeBounding, 0, 0,
                      shapePixmap, ShapeSet);

   JXFreeGC(display, shapeGC);
   JXFreePixmap(display, shapePixmap);

#endif

   UngrabServer();

}

/** Draw a client border. */
void DrawBorder(const ClientNode *np)
{

   /* Don't draw any more if we are shutting down. */
   if(JUNLIKELY(shouldExit)) {
      return;
   }

   /* Must be either mapped or shaded to have a border. */
   if(!(np->state.status & (STAT_MAPPED | STAT_SHADED))) {
      return;
   }

   /* Hidden and fullscreen windows don't get borders. */
   if(np->state.status & (STAT_HIDDEN | STAT_FULLSCREEN)) {
      return;
   }

   /* Return if there is no border. */
   if(!(np->state.border & (BORDER_TITLE | BORDER_OUTLINE))) {
      return;
   }

   /* Do the actual drawing. */
   DrawBorderHelper(np);

}

/** Helper method for drawing borders. */
void DrawBorderHelper(const ClientNode *np)
{

   ColorType borderTextColor;

   long titleColor1, titleColor2;
   long outlineColor;

   int north, south, east, west;
   unsigned int width, height;
   int iconSize;

   unsigned int buttonCount;
   int titleWidth;
   Pixmap canvas;

   iconSize = GetBorderIconSize();
   GetBorderSize(&np->state, &north, &south, &east, &west);
   width = np->width + east + west;
   if(np->state.status & STAT_SHADED) {
      height = north + south;
   } else {
      height = np->height + north + south;
   }

   /* Determine the colors and gradients to use. */
   if(np->state.status & (STAT_ACTIVE | STAT_FLASH)) {
      borderTextColor = COLOR_TITLE_ACTIVE_FG;
      titleColor1 = colors[COLOR_TITLE_ACTIVE_BG1];
      titleColor2 = colors[COLOR_TITLE_ACTIVE_BG2];
      outlineColor = colors[COLOR_BORDER_ACTIVE_LINE];
   } else {
      borderTextColor = COLOR_TITLE_FG;
      titleColor1 = colors[COLOR_TITLE_BG1];
      titleColor2 = colors[COLOR_TITLE_BG2];
      outlineColor = colors[COLOR_BORDER_LINE];
   }

   /* Set parent background to reduce flicker. */
   JXSetWindowBackground(display, np->parent, titleColor2);

   canvas = JXCreatePixmap(display, np->parent, width, north, rootDepth);

   /* Clear the window with the right color. */
   JXSetForeground(display, borderGC, titleColor2);
   JXFillRectangle(display, canvas, borderGC, 0, 0, width, north);

   /* Determine how many pixels may be used for the title. */
   buttonCount = GetButtonCount(np);
   titleWidth = width;
   titleWidth -= settings.titleHeight * buttonCount;
   titleWidth -= iconSize + 7 + 6;

   /* Draw the top part (either a title or north border). */
   if((np->state.border & BORDER_TITLE) &&
      settings.titleHeight > settings.borderWidth) {

      int starty = 0;
      const int startx = west + 1;
      if(settings.handles && !(np->state.status & STAT_VMAX)) {
         starty = west;
      }

      /* Draw a title bar. */
      DrawHorizontalGradient(canvas, borderGC, titleColor1, titleColor2,
                             1, 1, width - 2, settings.titleHeight - 2);

      /* Draw the icon. */
      if(np->icon && np->width >= settings.titleHeight) {
         PutIcon(np->icon, canvas, colors[borderTextColor], startx,
                 starty + (settings.titleHeight - iconSize) / 2,
                 iconSize, iconSize);
      }

      if(np->name && np->name[0] && titleWidth > 0) {
         const int sheight = GetStringHeight(FONT_BORDER);
         RenderString(canvas, FONT_BORDER, borderTextColor,
                      startx + settings.titleHeight + 4,
                      starty + (settings.titleHeight - sheight) / 2,
                      titleWidth, np->name);
      }

      DrawBorderButtons(np, canvas);

   }


   /* Copy the title bar to the window. */
   JXCopyArea(display, canvas, np->parent, borderGC, 1, 1,
              width - 2, north - 1, 1, 1);
   JXFreePixmap(display, canvas);

   /* Window outline.
    * These are drawn directly to the window.
    */
   JXClearArea(display, np->parent, 0, north,
               width, height - north, False);
   if(settings.handles) {
      DrawBorderHandles(np);
   } else {
      JXSetForeground(display, borderGC, outlineColor);
      if(np->state.status & STAT_SHADED) {
         DrawRoundedRectangle(np->parent, borderGC, 0, 0,
                              width - 1, north - 1,
                              settings.borderRadius);
      } else if(np->state.status & (STAT_HMAX | STAT_VMAX)) {
         JXDrawRectangle(display, np->parent, borderGC, 0, 0,
                         width - 1, height - 1);
      } else {
         DrawRoundedRectangle(np->parent, borderGC, 0, 0,
                              width - 1, height - 1,
                              settings.borderRadius);
      }
   }

}

/** Draw window handles. */
void DrawBorderHandles(const ClientNode *np)
{

   XSegment segments[8];
   long pixelUp, pixelDown;
   int width, height;
   int north, south, east, west;

   /* Don't draw handles if maximized. */
   if(np->state.status & STAT_VMAX) {
      return;
   }

   /* Determine the window size. */
   GetBorderSize(&np->state, &north, &south, &east, &west);
   width = np->width + east + west;
   if(np->state.status & STAT_SHADED) {
      height = north + south;
   } else {
      height = np->height + north + south;
   }

   /* Determine the colors to use. */
   if(np->state.status & (STAT_ACTIVE | STAT_FLASH)) {
      pixelUp = colors[COLOR_BORDER_ACTIVE_UP];
      pixelDown = colors[COLOR_BORDER_ACTIVE_DOWN];
   } else {
      pixelUp = colors[COLOR_BORDER_UP];
      pixelDown = colors[COLOR_BORDER_DOWN];
   }

   /* Top title border. */
   segments[0].x1 = settings.borderWidth;
   segments[0].y1 = settings.borderWidth;
   segments[0].x2 = width - settings.borderWidth - 1;
   segments[0].y2 = settings.borderWidth;

   /* Right title border. */
   segments[1].x1 = settings.borderWidth;
   segments[1].y1 = settings.borderWidth + 1;
   segments[1].x2 = settings.borderWidth;
   segments[1].y2 = settings.titleHeight + settings.borderWidth - 1;

   /* Inside right border. */
   segments[2].x1 = width - settings.borderWidth;
   segments[2].y1 = settings.borderWidth;
   segments[2].x2 = width - settings.borderWidth;
   segments[2].y2 = height - settings.borderWidth;

   /* Inside bottom border. */
   segments[3].x1 = settings.borderWidth;
   segments[3].y1 = height - settings.borderWidth;
   segments[3].x2 = width - settings.borderWidth + 1;
   segments[3].y2 = height - settings.borderWidth;

   /* Left border. */
   segments[4].x1 = 0;
   segments[4].y1 = 0;
   segments[4].x2 = 0;
   segments[4].y2 = height - 1;
   segments[5].x1 = 1;
   segments[5].y1 = 1;
   segments[5].x2 = 1;
   segments[5].y2 = height - 2;

   /* Top border. */
   segments[6].x1 = 1;
   segments[6].y1 = 0;
   segments[6].x2 = width - 1;
   segments[6].y2 = 0;
   segments[7].x1 = 1;
   segments[7].y1 = 1;
   segments[7].x2 = width - 2;
   segments[7].y2 = 1;

   /* Draw pixel-up segments. */
   JXSetForeground(display, borderGC, pixelUp);
   JXDrawSegments(display, np->parent, borderGC, segments, 8);

   /* Bottom title border. */
   segments[0].x1 = settings.borderWidth + 1;
   segments[0].y1 = settings.titleHeight + settings.borderWidth - 1;
   segments[0].x2 = width - settings.borderWidth;
   segments[0].y2 = settings.titleHeight + settings.borderWidth - 1;

   /* Right title border. */
   segments[1].x1 = width - settings.borderWidth - 1;
   segments[1].y1 = settings.borderWidth + 1;
   segments[1].x2 =  width - settings.borderWidth - 1;
   segments[1].y2 = settings.titleHeight + settings.borderWidth;

   /* Inside top border. */
   segments[2].x1 = settings.borderWidth - 1;
   segments[2].y1 = settings.borderWidth - 1;
   segments[2].x2 = width - settings.borderWidth;
   segments[2].y2 = settings.borderWidth - 1;

   /* Inside left border. */
   segments[3].x1 = settings.borderWidth - 1;
   segments[3].y1 = settings.borderWidth;
   segments[3].x2 = settings.borderWidth - 1;
   segments[3].y2 = height - settings.borderWidth;

   /* Right border. */
   segments[4].x1 = width - 1;
   segments[4].y1 = 0;
   segments[4].x2 = width - 1;
   segments[4].y2 = height - 1;
   segments[5].x1 = width - 2;
   segments[5].y1 = 1;
   segments[5].x2 = width - 2;
   segments[5].y2 = height - 2;

   /* Bottom border. */
   segments[6].x1 = 0;
   segments[6].y1 = height - 1;
   segments[6].x2 = width;
   segments[6].y2 = height - 1;
   segments[7].x1 = 1;
   segments[7].y1 = height - 2;
   segments[7].x2 = width - 1;
   segments[7].y2 = height - 2;

   /* Draw pixel-down segments. */
   JXSetForeground(display, borderGC, pixelDown);
   JXDrawSegments(display, np->parent, borderGC, segments, 8);

   /* Draw marks */
   if((np->state.border & BORDER_RESIZE)
      && !(np->state.status & (STAT_SHADED | STAT_HMAX | STAT_VMAX))) {

      /* Upper left */
      segments[0].x1 = settings.titleHeight + settings.borderWidth - 1;
      segments[0].y1 = 0;
      segments[0].x2 = settings.titleHeight + settings.borderWidth - 1;
      segments[0].y2 = settings.borderWidth;
      segments[1].x1 = 0;
      segments[1].y1 = settings.titleHeight + settings.borderWidth - 1;
      segments[1].x2 = settings.borderWidth;
      segments[1].y2 = settings.titleHeight + settings.borderWidth - 1;
   
      /* Upper right. */
      segments[2].x1 = width - settings.borderWidth;
      segments[2].y1 = settings.titleHeight + settings.borderWidth - 1;
      segments[2].x2 = width;
      segments[2].y2 = settings.titleHeight + settings.borderWidth - 1;
      segments[3].x1 = width - settings.titleHeight - settings.borderWidth - 1;
      segments[3].y1 = 0;
      segments[3].x2 = width - settings.titleHeight - settings.borderWidth - 1;
      segments[3].y2 = settings.borderWidth;

      /* Lower left */
      segments[4].x1 = 0;
      segments[4].y1 = height - settings.titleHeight - settings.borderWidth - 1;
      segments[4].x2 = settings.borderWidth;
      segments[4].y2 = height - settings.titleHeight - settings.borderWidth - 1;
      segments[5].x1 = settings.titleHeight + settings.borderWidth - 1;
      segments[5].y1 = height - settings.borderWidth;
      segments[5].x2 = settings.titleHeight + settings.borderWidth - 1;
      segments[5].y2 = height;

      /* Lower right */
      segments[6].x1 = width - settings.borderWidth;
      segments[6].y1 = height - settings.titleHeight - settings.borderWidth - 1;
      segments[6].x2 = width;
      segments[6].y2 = height - settings.titleHeight - settings.borderWidth - 1;
      segments[7].x1 = width - settings.titleHeight - settings.borderWidth - 1;
      segments[7].y1 = height - settings.borderWidth;
      segments[7].x2 = width - settings.titleHeight - settings.borderWidth - 1;
      segments[7].y2 = height;

      /* Draw pixel-down segments. */
      JXSetForeground(display, borderGC, pixelDown);
      JXDrawSegments(display, np->parent, borderGC, segments, 8);

      /* Upper left */
      segments[0].x1 = settings.titleHeight + settings.borderWidth;
      segments[0].y1 = 0;
      segments[0].x2 = settings.titleHeight + settings.borderWidth;
      segments[0].y2 = settings.borderWidth;
      segments[1].x1 = 0;
      segments[1].y1 = settings.titleHeight + settings.borderWidth;
      segments[1].x2 = settings.borderWidth;
      segments[1].y2 = settings.titleHeight + settings.borderWidth;
   
      /* Upper right */
      segments[2].x1 = width - settings.titleHeight - settings.borderWidth;
      segments[2].y1 = 0;
      segments[2].x2 = width - settings.titleHeight - settings.borderWidth;
      segments[2].y2 = settings.borderWidth;
      segments[3].x1 = width - settings.borderWidth;
      segments[3].y1 = settings.titleHeight + settings.borderWidth;
      segments[3].x2 = width;
      segments[3].y2 = settings.titleHeight + settings.borderWidth;

      /* Lower left */
      segments[4].x1 = 0;
      segments[4].y1 = height - settings.titleHeight - settings.borderWidth;
      segments[4].x2 = settings.borderWidth;
      segments[4].y2 = height - settings.titleHeight - settings.borderWidth;
      segments[5].x1 = settings.titleHeight + settings.borderWidth;
      segments[5].y1 = height - settings.borderWidth;
      segments[5].x2 = settings.titleHeight + settings.borderWidth;
      segments[5].y2 = height;

      /* Lower right */
      segments[6].x1 = width - settings.borderWidth;
      segments[6].y1 = height - settings.titleHeight - settings.borderWidth;
      segments[6].x2 = width;
      segments[6].y2 = height - settings.titleHeight - settings.borderWidth;
      segments[7].x1 = width - settings.titleHeight - settings.borderWidth;
      segments[7].y1 = height - settings.borderWidth;
      segments[7].x2 = width - settings.titleHeight - settings.borderWidth;
      segments[7].y2 = height;

      /* Draw pixel-up segments. */
      JXSetForeground(display, borderGC, pixelUp);
      JXDrawSegments(display, np->parent, borderGC, segments, 8);

   }
}

/** Determine the number of buttons to be displayed for a client. */
unsigned int GetButtonCount(const ClientNode *np)
{

   int north, south, east, west;
   unsigned int count;
   int offset;

   if(!(np->state.border & BORDER_TITLE)) {
      return 0;
   }
   if(settings.titleHeight <= settings.borderWidth) {
      return 0;
   }

   GetBorderSize(&np->state, &north, &south, &east, &west);

   offset = np->width + west;
   if(offset <= 2 * settings.titleHeight) {
      return 0;
   }

   count = 0;
   if(np->state.border & BORDER_CLOSE) {
      count += 1;
      if(offset <= 2 * settings.titleHeight) {
         return count;
      }
      offset -= settings.titleHeight;
   }

   if(np->state.border & BORDER_MAX) {
      count += 1;
      if(offset < 2 * settings.titleHeight) {
         return count;
      }
   }

   if(np->state.border & BORDER_MIN) {
      count += 1;
   }

   return count;

}

/** Draw the buttons on a client frame. */
void DrawBorderButtons(const ClientNode *np, Pixmap canvas)
{

   long color;
   long pixelUp, pixelDown;
   int xoffset, yoffset;
   int north, south, east, west;

   Assert(np);

   GetBorderSize(&np->state, &north, &south, &east, &west);
   xoffset = np->width + west - settings.titleHeight;
   if(xoffset <= settings.titleHeight) {
      return;
   }

   /* Determine the colors to use. */
   if(np->state.status & (STAT_ACTIVE | STAT_FLASH)) {
      color = colors[COLOR_TITLE_ACTIVE_FG];
      pixelUp = colors[COLOR_BORDER_ACTIVE_UP];
      pixelDown = colors[COLOR_BORDER_ACTIVE_DOWN];
   } else {
      color = colors[COLOR_TITLE_FG];
      pixelUp = colors[COLOR_BORDER_UP];
      pixelDown = colors[COLOR_BORDER_DOWN];
   }

   if(settings.handles) {
      JXSetForeground(display, borderGC, pixelDown);
      JXDrawLine(display, canvas, borderGC,
                 west + settings.titleHeight - 1,
                 south,
                 west + settings.titleHeight - 1,
                 south + settings.titleHeight);
      JXDrawLine(display, canvas, borderGC, xoffset - 1,
                 south, xoffset - 1,
                 south + settings.titleHeight);
      JXSetForeground(display, borderGC, pixelUp);
      JXDrawLine(display, canvas, borderGC,
                 east + settings.titleHeight,
                 south,
                 east + settings.titleHeight,
                 south + settings.titleHeight);
      JXDrawLine(display, canvas, borderGC, xoffset,
                 south, xoffset,
                 south + settings.titleHeight);
      yoffset = south - 1;
      xoffset -= 1;
   } else {
      yoffset = 0;
   }

   /* Close button. */
   if(np->state.border & BORDER_CLOSE) {
      JXSetForeground(display, borderGC, color);
      DrawCloseButton(xoffset, yoffset, canvas);
      xoffset -= settings.titleHeight;
      if(xoffset <= settings.titleHeight) {
         return;
      }
   }

   if(settings.handles) {
      JXSetForeground(display, borderGC, pixelDown);
      JXDrawLine(display, canvas, borderGC, xoffset - 1,
                 south, xoffset - 1,
                 south + settings.titleHeight);
      JXSetForeground(display, borderGC, pixelUp);
      JXDrawLine(display, canvas, borderGC, xoffset,
                 south, xoffset,
                 south + settings.titleHeight);
   }

   /* Maximize button. */
   if(np->state.border & BORDER_MAX) {
      JXSetForeground(display, borderGC, color);
      if(np->state.status & (STAT_HMAX | STAT_VMAX)) {
         DrawMaxAButton(xoffset, yoffset, canvas);
      } else {
         DrawMaxIButton(xoffset, yoffset, canvas);
      }
      xoffset -= settings.titleHeight;
      if(xoffset <= settings.titleHeight) {
         return;
      }
   }

   if(settings.handles) {
      JXSetForeground(display, borderGC, pixelDown);
      JXDrawLine(display, canvas, borderGC, xoffset - 1,
                 south, xoffset - 1,
                 south + settings.titleHeight);
      JXSetForeground(display, borderGC, pixelUp);
      JXDrawLine(display, canvas, borderGC, xoffset,
                 south, xoffset,
                 south + settings.titleHeight);
   }

   /* Minimize button. */
   if(np->state.border & BORDER_MIN) {
      JXSetForeground(display, borderGC, color);
      DrawMinButton(xoffset, yoffset, canvas);
   }

}

/** Attempt to draw a border icon. */
char DrawBorderIcon(BorderIconType t, unsigned int xoffset,
                    unsigned int yoffset, Pixmap canvas)
{
   if(buttonIcons[t]) {
      ButtonNode button;
      ResetButton(&button, canvas, borderGC);
      button.x       = xoffset;
      button.y       = yoffset;
      button.width   = settings.titleHeight;
      button.height  = settings.titleHeight;
      button.icon    = buttonIcons[t];
      button.border  = 0;
      DrawButton(&button);
      return 1;
   } else {
      return 0;
   }
}

/** Draw a close button. */
void DrawCloseButton(unsigned int xoffset, unsigned int yoffset,
                     Pixmap canvas)
{
   XSegment segments[2];
   unsigned int size;
   unsigned int x1, y1;
   unsigned int x2, y2;

   if(DrawBorderIcon(BI_CLOSE, xoffset, yoffset, canvas)) {
      return;
   }

   size = (settings.titleHeight + 2) / 3;
   x1 = xoffset + settings.titleHeight / 2 - size / 2;
   y1 = yoffset + settings.titleHeight / 2 - size / 2;
   x2 = x1 + size;
   y2 = y1 + size;

   segments[0].x1 = x1;
   segments[0].y1 = y1;
   segments[0].x2 = x2;
   segments[0].y2 = y2;

   segments[1].x1 = x2;
   segments[1].y1 = y1;
   segments[1].x2 = x1;
   segments[1].y2 = y2;

   JXSetLineAttributes(display, borderGC, 2, LineSolid,
                       CapProjecting, JoinBevel);
   JXDrawSegments(display, canvas, borderGC, segments, 2);
   JXSetLineAttributes(display, borderGC, 1, LineSolid,
                       CapNotLast, JoinMiter);

}

/** Draw an inactive maximize button. */
void DrawMaxIButton(unsigned int xoffset, unsigned int yoffset,
                    Pixmap canvas)
{

   XSegment segments[5];
   unsigned int size;
   unsigned int x1, y1;
   unsigned int x2, y2;

   if(DrawBorderIcon(BI_MAX, xoffset, yoffset, canvas)) {
      return;
   }

   size = 2 + (settings.titleHeight + 2) / 3;
   x1 = xoffset + settings.titleHeight / 2 - size / 2;
   y1 = yoffset + settings.titleHeight / 2 - size / 2;
   x2 = x1 + size;
   y2 = y1 + size;

   segments[0].x1 = x1;
   segments[0].y1 = y1;
   segments[0].x2 = x1 + size;
   segments[0].y2 = y1;

   segments[1].x1 = x1;
   segments[1].y1 = y1 + 1;
   segments[1].x2 = x1 + size;
   segments[1].y2 = y1 + 1;

   segments[2].x1 = x1;
   segments[2].y1 = y1;
   segments[2].x2 = x1;
   segments[2].y2 = y2;

   segments[3].x1 = x2;
   segments[3].y1 = y1;
   segments[3].x2 = x2;
   segments[3].y2 = y2;

   segments[4].x1 = x1;
   segments[4].y1 = y2;
   segments[4].x2 = x2;
   segments[4].y2 = y2;

   JXSetLineAttributes(display, borderGC, 1, LineSolid,
                       CapProjecting, JoinMiter);
   JXDrawSegments(display, canvas, borderGC, segments, 5);
   JXSetLineAttributes(display, borderGC, 1, LineSolid,
                       CapButt, JoinMiter);

}

/** Draw an active maximize button. */
void DrawMaxAButton(unsigned int xoffset, unsigned int yoffset,
                    Pixmap canvas)
{
   XSegment segments[8];
   unsigned int size;
   unsigned int x1, y1;
   unsigned int x2, y2;
   unsigned int x3, y3;

   if(DrawBorderIcon(BI_MAX_ACTIVE, xoffset, yoffset, canvas)) {
      return;
   }

   size = 2 + (settings.titleHeight + 2) / 3;
   x1 = xoffset + settings.titleHeight / 2 - size / 2;
   y1 = yoffset + settings.titleHeight / 2 - size / 2;
   x2 = x1 + size;
   y2 = y1 + size;
   x3 = x1 + size / 2;
   y3 = y1 + size / 2;

   segments[0].x1 = x1;
   segments[0].y1 = y1;
   segments[0].x2 = x2;
   segments[0].y2 = y1;

   segments[1].x1 = x1;
   segments[1].y1 = y1 + 1;
   segments[1].x2 = x2;
   segments[1].y2 = y1 + 1;

   segments[2].x1 = x1;
   segments[2].y1 = y1;
   segments[2].x2 = x1;
   segments[2].y2 = y2;

   segments[3].x1 = x2;
   segments[3].y1 = y1;
   segments[3].x2 = x2;
   segments[3].y2 = y2;

   segments[4].x1 = x1;
   segments[4].y1 = y2;
   segments[4].x2 = x2;
   segments[4].y2 = y2;

   segments[5].x1 = x1;
   segments[5].y1 = y3;
   segments[5].x2 = x3;
   segments[5].y2 = y3;

   segments[6].x1 = x1;
   segments[6].y1 = y3 + 1;
   segments[6].x2 = x3;
   segments[6].y2 = y3 + 1;

   segments[7].x1 = x3;
   segments[7].y1 = y3;
   segments[7].x2 = x3;
   segments[7].y2 = y2;

   JXSetLineAttributes(display, borderGC, 1, LineSolid,
                       CapProjecting, JoinMiter);
   JXDrawSegments(display, canvas, borderGC, segments, 8);
   JXSetLineAttributes(display, borderGC, 1, LineSolid,
                       CapButt, JoinMiter);
}

/** Draw a minimize button. */
void DrawMinButton(unsigned int xoffset, unsigned int yoffset, Pixmap canvas)
{

   unsigned int size;
   unsigned int x1, y1;
   unsigned int x2, y2;

   if(DrawBorderIcon(BI_MIN, xoffset, yoffset, canvas)) {
      return;
   }

   size = (settings.titleHeight + 2) / 3;
   x1 = xoffset + settings.titleHeight / 2 - size / 2;
   y1 = yoffset + settings.titleHeight / 2 - size / 2;
   x2 = x1 + size;
   y2 = y1 + size;

   JXSetLineAttributes(display, borderGC, 2, LineSolid,
                       CapProjecting, JoinMiter);
   JXDrawLine(display, canvas, borderGC, x1, y2, x2, y2);
   JXSetLineAttributes(display, borderGC, 1, LineSolid, CapButt, JoinMiter);

}

/** Redraw the borders on the current desktop.
 * This should be done after loading clients since the stacking order
 * may cause borders on the current desktop to become visible after moving
 * clients to their assigned desktops.
 */
void ExposeCurrentDesktop(void)
{

   ClientNode *np;
   int layer;

   for(layer = 0; layer < LAYER_COUNT; layer++) {
      for(np = nodes[layer]; np; np = np->next) {
         if(!(np->state.status & (STAT_HIDDEN | STAT_MINIMIZED))) {
            DrawBorder(np);
         }
      }
   }

}

/** Get the size of the borders for a client. */
void GetBorderSize(const ClientState *state,
                   int *north, int *south, int *east, int *west)
{

   Assert(state);
   Assert(north);
   Assert(south);
   Assert(east);
   Assert(west);

   *north = 0;
   *south = 0;
   *east = 0;
   *west = 0;

   /* Full screen is a special case. */
   if(state->status & STAT_FULLSCREEN) {
      return;
   }

   if(state->border & BORDER_OUTLINE) {
      if(!(state->status & STAT_VMAX)) {
         *north = settings.borderWidth;
         *south = settings.borderWidth;
      }
      if(!(state->status & STAT_HMAX)) {
         *east = settings.borderWidth;
         *west = settings.borderWidth;
      }
   }

   if(state->border & BORDER_TITLE) {
      if(settings.handles) {
         *north += settings.titleHeight;
      } else {
         *north = settings.titleHeight;
      }
   }
   if(!settings.handles && (state->status & STAT_SHADED)) {
      *south = 0;
   }

}

/** Draw a rounded rectangle. */
void DrawRoundedRectangle(Drawable d, GC gc, int x, int y,
                          int width, int height, int radius)
{
#ifdef USE_SHAPE
#ifdef USE_XMU

   XmuDrawRoundedRectangle(display, d, gc, x, y, width, height,
                           radius, radius);

#else

   XSegment segments[4];
   XArc     arcs[4];

   segments[0].x1 = x + radius;           segments[0].y1 = y;
   segments[0].x2 = x + width - radius;   segments[0].y2 = y;
   segments[1].x1 = x + radius;           segments[1].y1 = y + height;
   segments[1].x2 = x + width - radius;   segments[1].y2 = y + height;
   segments[2].x1 = x;                    segments[2].y1 = y + radius;
   segments[2].x2 = x;                    segments[2].y2 = y + height - radius;
   segments[3].x1 = x + width;            segments[3].y1 = y + radius;
   segments[3].x2 = x + width;            segments[3].y2 = y + height - radius;
   JXDrawSegments(display, d, gc, segments, 4);

   arcs[0].x = x;
   arcs[0].y = y;
   arcs[0].width = radius * 2;
   arcs[0].height = radius * 2;
   arcs[0].angle1 = 90 * 64;
   arcs[0].angle2 = 90 * 64;
   arcs[1].x = x + width - radius * 2;
   arcs[1].y = y;
   arcs[1].width  = radius * 2;
   arcs[1].height = radius * 2;
   arcs[1].angle1 = 0 * 64;
   arcs[1].angle2 = 90 * 64;
   arcs[2].x = x;
   arcs[2].y = y + height - radius * 2;
   arcs[2].width  = radius * 2;
   arcs[2].height = radius * 2;
   arcs[2].angle1 = 180 * 64;
   arcs[2].angle2 = 90 * 64;
   arcs[3].x = x + width - radius * 2;
   arcs[3].y = y + height - radius * 2;
   arcs[3].width  = radius * 2;
   arcs[3].height = radius * 2;
   arcs[3].angle1 = 270 * 64;
   arcs[3].angle2 = 90 * 64;
   JXDrawArcs(display, d, gc, arcs, 4);

#endif
#else

   JXDrawRectangle(display, d, gc, x, y, width, height);
   
#endif
}

/** Fill a rounded rectangle. */
#ifdef USE_SHAPE
void FillRoundedRectangle(Drawable d, GC gc, int x, int y,
                          int width, int height, int radius)
{

#ifdef USE_XMU

   XmuFillRoundedRectangle(display, d, gc, x, y, width, height,
                           radius, radius);

#else

   XRectangle  rects[3];
   XArc        arcs[4];

   rects[0].x = x + radius;
   rects[0].y = y;
   rects[0].width = width - radius * 2;
   rects[0].height = radius;
   rects[1].x = x;
   rects[1].y = radius;
   rects[1].width = width;
   rects[1].height = height - radius * 2;
   rects[2].x = x + radius;
   rects[2].y = y + height - radius;
   rects[2].width = width - radius * 2;
   rects[2].height = radius;
   JXFillRectangles(display, d, gc, rects, 3);

   arcs[0].x = x;
   arcs[0].y = y;
   arcs[0].width = radius * 2;
   arcs[0].height = radius * 2;
   arcs[0].angle1 = 90 * 64;
   arcs[0].angle2 = 90 * 64;
   arcs[1].x = x + width - radius * 2 - 1;
   arcs[1].y = y;
   arcs[1].width  = radius * 2;
   arcs[1].height = radius * 2;
   arcs[1].angle1 = 0 * 64;
   arcs[1].angle2 = 90 * 64;
   arcs[2].x = x;
   arcs[2].y = y + height - radius * 2 - 1;
   arcs[2].width  = radius * 2;
   arcs[2].height = radius * 2;
   arcs[2].angle1 = 180 * 64;
   arcs[2].angle2 = 90 * 64;
   arcs[3].x = x + width - radius * 2 - 1;
   arcs[3].y = y + height - radius * 2 -1;
   arcs[3].width  = radius * 2;
   arcs[3].height = radius * 2;
   arcs[3].angle1 = 270 * 64;
   arcs[3].angle2 = 90 * 64;
   JXFillArcs(display, d, gc, arcs, 4);

#endif

}
#endif

/** Set the icon to use for a button. */
void SetBorderIcon(BorderIconType t, const char *name)
{
   buttonNames[t] = CopyString(name);
}
