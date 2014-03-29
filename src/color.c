/**
 * @file color.c
 * @author Joe Wingbermuehle
 * @date 2004-2006
 *
 * @brief Functions to handle loading colors.
 *
 */

#include "jwm.h"
#include "main.h"
#include "color.h"
#include "error.h"
#include "misc.h"

/** Mapping between color types and default values. */
typedef struct {
   ColorType type;
   unsigned int value;
} DefaultColorNode;

/** Mapping from color type to the value it from which it inherits. */
typedef struct {
   ColorType dest;
   ColorType src;
} ColorInheritNode;

unsigned long colors[COLOR_COUNT];
static unsigned long rgbColors[COLOR_COUNT];

/* Map a linear 8-bit RGB space to pixel values. */
static unsigned long *map;

/* Map 8-bit pixel values to a 24-bit linear RGB space. */
static unsigned long *rmap;

#ifdef USE_XFT
static XftColor *xftColors[COLOR_COUNT] = { NULL };
#endif

static const DefaultColorNode DEFAULT_COLORS[] = {

   { COLOR_TITLE_FG,                0xFFFFFF    },
   { COLOR_TITLE_ACTIVE_FG,         0xFFFFFF    },

   { COLOR_TITLE_BG1,               0x333333    },
   { COLOR_TITLE_BG2,               0x111111    },
   { COLOR_TITLE_ACTIVE_BG1,        0xCC7700    },
   { COLOR_TITLE_ACTIVE_BG2,        0x884400    },

   { COLOR_BORDER_LINE,             0x000000    },
   { COLOR_BORDER_ACTIVE_LINE,      0x000000    },

   { COLOR_TRAY_FG,                 0xFFFFFF    },
   { COLOR_TRAY_BG1,                0x333333    },
   { COLOR_TRAY_BG2,                0x111111    },
   { COLOR_TRAY_ACTIVE_FG,          0xFFFFFF    },
   { COLOR_TRAY_ACTIVE_BG1,         0x111111    },
   { COLOR_TRAY_ACTIVE_BG2,         0x333333    },

   { COLOR_TASK_FG,                 0xFFFFFF    },
   { COLOR_TASK_BG1,                0x333333    },
   { COLOR_TASK_BG2,                0x111111    },
   { COLOR_TASK_ACTIVE_FG,          0xFFFFFF    },
   { COLOR_TASK_ACTIVE_BG1,         0x111111    },
   { COLOR_TASK_ACTIVE_BG2,         0x333333    },

   { COLOR_PAGER_BG,                0x111111    },
   { COLOR_PAGER_FG,                0x444444    },
   { COLOR_PAGER_ACTIVE_BG,         0x884400    },
   { COLOR_PAGER_ACTIVE_FG,         0xCC7700    },
   { COLOR_PAGER_OUTLINE,           0x000000    },
   { COLOR_PAGER_TEXT,              0xFFFFFF    },

   { COLOR_MENU_BG,                 0x333333    },
   { COLOR_MENU_FG,                 0xFFFFFF    },
   { COLOR_MENU_ACTIVE_BG1,         0xCC7700    },
   { COLOR_MENU_ACTIVE_BG2,         0x884400    },
   { COLOR_MENU_ACTIVE_FG,          0xFFFFFF    },

   { COLOR_POPUP_BG,                0x999999    },
   { COLOR_POPUP_FG,                0x000000    },
   { COLOR_POPUP_OUTLINE,           0x000000    },

   { COLOR_TRAYBUTTON_FG,           0xFFFFFF    },
   { COLOR_TRAYBUTTON_BG1,          0x333333    },
   { COLOR_TRAYBUTTON_BG2,          0x111111    },
   { COLOR_TRAYBUTTON_ACTIVE_FG,    0xFFFFFF    },
   { COLOR_TRAYBUTTON_ACTIVE_BG1,   0x111111    },
   { COLOR_TRAYBUTTON_ACTIVE_BG2,   0x333333    },

   { COLOR_CLOCK_FG,                0xFFFFFF    },
   { COLOR_CLOCK_BG1,               0x333333    },
   { COLOR_CLOCK_BG2,               0x111111    }

};
static const unsigned int DEFAULT_COUNT
   = sizeof(DEFAULT_COLORS) / sizeof(DEFAULT_COLORS[0]);

static const ColorInheritNode INHERIT_COLORS[] = {
   { COLOR_TASK_FG,                 COLOR_TRAY_FG           },
   { COLOR_TASK_BG1,                COLOR_TRAY_BG1          },
   { COLOR_TASK_BG2,                COLOR_TRAY_BG2          },
   { COLOR_TASK_ACTIVE_FG,          COLOR_TRAY_ACTIVE_FG    },
   { COLOR_TASK_ACTIVE_BG1,         COLOR_TRAY_ACTIVE_BG1   },
   { COLOR_TASK_ACTIVE_BG2,         COLOR_TRAY_ACTIVE_BG2   },
   { COLOR_TRAYBUTTON_FG,           COLOR_TRAY_FG           },
   { COLOR_TRAYBUTTON_BG1,          COLOR_TRAY_BG1          },
   { COLOR_TRAYBUTTON_BG2,          COLOR_TRAY_BG2,         },
   { COLOR_TRAYBUTTON_ACTIVE_FG,    COLOR_TRAY_ACTIVE_FG    },
   { COLOR_TRAYBUTTON_ACTIVE_BG1,   COLOR_TRAY_ACTIVE_BG1   },
   { COLOR_TRAYBUTTON_ACTIVE_BG2,   COLOR_TRAY_ACTIVE_BG2   },
   { COLOR_CLOCK_FG,                COLOR_TRAY_FG           },
   { COLOR_CLOCK_BG1,               COLOR_TRAY_BG1          },
   { COLOR_CLOCK_BG2,               COLOR_TRAY_BG2          }
};
static const unsigned int INHERIT_COUNT
   = sizeof(INHERIT_COLORS) / sizeof(INHERIT_COLORS[0]);

static char **names = NULL;

static unsigned long redShift;
static unsigned long greenShift;
static unsigned long blueShift;
static unsigned long redMask;
static unsigned long greenMask;
static unsigned long blueMask;

static void ComputeShiftMask(unsigned long maskIn,
                             unsigned long *shiftOut,
                             unsigned long *maskOut);

static void GetDirectPixel(XColor *c);
static void GetMappedPixel(XColor *c);

static void SetDefaultColor(ColorType type); 

static unsigned long ReadHex(const char *hex);

static unsigned long GetRGBFromXColor(const XColor *c);
static XColor GetXColorFromRGB(unsigned long rgb);

static int GetColorByName(const char *str, XColor *c);
static void InitializeNames(void);

static void LightenColor(ColorType oldColor, ColorType newColor);
static void DarkenColor(ColorType oldColor, ColorType newColor);

/** Startup color support. */
void StartupColors(void)
{

   unsigned int x;
   int red, green, blue;
   XColor c;

   /* Determine how to convert between RGB triples and pixels. */
   Assert(rootVisual);
   switch(rootVisual->class) {
   case DirectColor:
   case TrueColor:
      ComputeShiftMask(rootVisual->red_mask, &redShift, &redMask);
      ComputeShiftMask(rootVisual->green_mask, &greenShift, &greenMask);
      ComputeShiftMask(rootVisual->blue_mask, &blueShift, &blueMask);
      map = NULL;
      break;
   default:

      /* Attempt to get 256 colors, pretend it worked. */
      redMask = 0xE0;
      greenMask = 0x1C;
      blueMask = 0x03;
      ComputeShiftMask(redMask, &redShift, &redMask);
      ComputeShiftMask(greenMask, &greenShift, &greenMask);
      ComputeShiftMask(blueMask, &blueShift, &blueMask);
      map = Allocate(sizeof(unsigned long) * 256);

      /* RGB: 3, 3, 2 */
      x = 0;
      for(red = 0; red < 8; red++) {
         for(green = 0; green < 8; green++) {
            for(blue = 0; blue < 4; blue++) {
               c.red = (unsigned short)(74898 * red / 8);
               c.green = (unsigned short)(74898 * green / 8);
               c.blue = (unsigned short)(87381 * blue / 4);
               c.flags = DoRed | DoGreen | DoBlue;
               JXAllocColor(display, rootColormap, &c);
               map[x] = c.pixel;
               ++x;
            }
         }
      }

      /* Compute the reverse pixel mapping (pixel -> 24-bit RGB). */
      rmap = Allocate(sizeof(unsigned long) * 256);
      for(x = 0; x < 256; x++) {
         c.pixel = x;
         JXQueryColor(display, rootColormap, &c);
         GetDirectPixel(&c);
         rmap[x] = c.pixel;
      }

      break;
   }

   /* Inherit unset colors. */
   if(names) {
      for(x = 0; x < INHERIT_COUNT; x++) {
         if(!names[INHERIT_COLORS[x].dest]) {
            names[INHERIT_COLORS[x].dest]
               = CopyString(names[INHERIT_COLORS[x].src]);
         }
      }
   }

   /* Get color information used for JWM stuff. */
   for(x = 0; x < COLOR_COUNT; x++) {
      if(names && names[x]) {
         if(ParseColor(names[x], &c)) {
            colors[x] = c.pixel;
            rgbColors[x] = GetRGBFromXColor(&c);
         } else {
            SetDefaultColor(x);
         }
      } else {
         SetDefaultColor(x);
      }
   }

   /* If not explicity set, select an outline for active menu items. */
   if(!names || !names[COLOR_MENU_ACTIVE_OL]) {
      DarkenColor(COLOR_MENU_ACTIVE_BG1, COLOR_MENU_ACTIVE_OL);
   }

   LightenColor(COLOR_TRAY_BG1, COLOR_TRAY_UP);
   DarkenColor(COLOR_TRAY_BG1, COLOR_TRAY_DOWN);

   LightenColor(COLOR_TASK_BG1, COLOR_TASK_UP);
   DarkenColor(COLOR_TASK_BG1, COLOR_TASK_DOWN);

   LightenColor(COLOR_TASK_ACTIVE_BG1, COLOR_TASK_ACTIVE_UP);
   DarkenColor(COLOR_TASK_ACTIVE_BG1, COLOR_TASK_ACTIVE_DOWN);
  
   LightenColor(COLOR_TRAYBUTTON_BG1, COLOR_TRAYBUTTON_UP);
   DarkenColor(COLOR_TRAYBUTTON_BG1, COLOR_TRAYBUTTON_DOWN);

   LightenColor(COLOR_TRAYBUTTON_ACTIVE_BG1, COLOR_TRAYBUTTON_ACTIVE_UP);
   DarkenColor(COLOR_TRAYBUTTON_ACTIVE_BG1, COLOR_TRAYBUTTON_ACTIVE_DOWN);

   LightenColor(COLOR_MENU_BG, COLOR_MENU_UP);
   DarkenColor(COLOR_MENU_BG, COLOR_MENU_DOWN);

   LightenColor(COLOR_TITLE_BG1, COLOR_BORDER_UP);
   DarkenColor(COLOR_TITLE_BG1, COLOR_BORDER_DOWN);

   LightenColor(COLOR_TITLE_ACTIVE_BG1, COLOR_BORDER_ACTIVE_UP);
   DarkenColor(COLOR_TITLE_ACTIVE_BG1, COLOR_BORDER_ACTIVE_DOWN);

   if(names) {
      for(x = 0; x < COLOR_COUNT; x++) {
         if(names[x]) {
            Release(names[x]);
         }
      }
      Release(names);
      names = NULL;
   }

}

/** Shutdown color support. */
void ShutdownColors(void)
{

#ifdef USE_XFT

   int x;

   for(x = 0; x < COLOR_COUNT; x++) {
      if(xftColors[x]) {
         JXftColorFree(display, rootVisual, rootColormap, xftColors[x]);
         Release(xftColors[x]);
         xftColors[x] = NULL;
      }
   }

#endif

   if(map != NULL) {
      JXFreeColors(display, rootColormap, map, 256, 0);
      Release(map);
      map = NULL;
      Release(rmap);
      rmap = NULL;
   }

}

/** Release color data. */
void DestroyColors(void)
{

   if(names) {
      unsigned int x;
      for(x = 0; x < COLOR_COUNT; x++) {
         if(names[x]) {
            Release(names[x]);
         }
      }
      Release(names);
      names = NULL;
   }

}

/** Compute the mask for computing colors in a linear RGB colormap. */
void ComputeShiftMask(unsigned long maskIn, unsigned long *shiftOut,
                      unsigned long *maskOut)
{

   unsigned int shift;

   Assert(shiftOut);
   Assert(maskOut);

   /* Components are stored in 16 bits.
    * When computing pixels, we'll first shift left 16 bits
    * so to the shift will be an offset from that 32 bit entity.
    * shift = 16 - <shift-to-ones> + <shift-to-zeros>
    */

   shift = 0;
   *maskOut = maskIn;
   while(maskIn && (maskIn & (1 << 31)) == 0) {
      shift += 1;
      maskIn <<= 1;
   }
   *shiftOut = shift;

}

/** Get an RGB value from an XColor. */
unsigned long GetRGBFromXColor(const XColor *c)
{

   unsigned int red, green, blue;
   unsigned long rgb;

   Assert(c);

   red = Min((c->red + 0x80) >> 8, 0xFF);
   green = Min((c->green + 0x80) >> 8, 0xFF);
   blue = Min((c->blue + 0x80) >> 8, 0xFF);

   rgb = (unsigned long)red << 16;
   rgb |= (unsigned long)green << 8;
   rgb |= (unsigned long)blue;

   return rgb;

}

/** Convert an RGB value to an XColor. */
XColor GetXColorFromRGB(unsigned long rgb)
{

   XColor ret = { 0 };

   ret.flags = DoRed | DoGreen | DoBlue;
   ret.red = (unsigned short)(((rgb >> 16) & 0xFF) * 257);
   ret.green = (unsigned short)(((rgb >> 8) & 0xFF) * 257);
   ret.blue = (unsigned short)((rgb & 0xFF) * 257);

   return ret;

}

/** Set the color to use for a component. */
void SetColor(ColorType c, const char *value)
{

   if(JUNLIKELY(!value)) {
      Warning("empty color tag");
      return;
   }

   InitializeNames();

   if(names[c]) {
      Release(names[c]);
   }

   names[c] = CopyString(value);

}

/** Parse a color for a component. */
char ParseColor(const char *value, XColor *c)
{


   if(JUNLIKELY(!value)) {
      return 0;
   }

   if(value[0] == '#' && strlen(value) == 7) {
      const unsigned long rgb = ReadHex(value + 1);
      c->red = ((rgb >> 16) & 0xFF) * 257;
      c->green = ((rgb >> 8) & 0xFF) * 257;
      c->blue = (rgb & 0xFF) * 257;
      c->flags = DoRed | DoGreen | DoBlue;
      GetColor(c);
   } else {
      if(JUNLIKELY(!GetColorByName(value, c))) {
         Warning("bad color: \"%s\"", value);
         return 0;
      }
   }

   return 1;

}

/** Set the specified color to its default. */
void SetDefaultColor(ColorType type)
{

   XColor c;
   unsigned int x;

   for(x = 0; x < DEFAULT_COUNT; x++) {
      if(DEFAULT_COLORS[x].type == type) {
         const unsigned int rgb = DEFAULT_COLORS[x].value;
         c.red = ((rgb >> 16) & 0xFF) * 257;
         c.green = ((rgb >> 8) & 0xFF) * 257;
         c.blue = (rgb & 0xFF) * 257;
         c.flags = DoRed | DoGreen | DoBlue;
         GetColor(&c);
         colors[type] = c.pixel;
         rgbColors[type] = rgb;
         return;
      }
   }

}

/** Initialize color names to NULL. */
void InitializeNames(void)
{

   if(names == NULL) {
      unsigned int x;
      names = Allocate(sizeof(char*) * COLOR_COUNT);
      for(x = 0; x < COLOR_COUNT; x++) {
         names[x] = NULL;
      }
   }

}

/** Convert a hex value to an unsigned long. */
unsigned long ReadHex(const char *hex)
{

   unsigned long value = 0;
   unsigned int x;

   Assert(hex);

   for(x = 0; hex[x]; x++) {
      value *= 16;
      if(hex[x] >= '0' && hex[x] <= '9') {
         value += hex[x] - '0';
      } else if(hex[x] >= 'A' && hex[x] <= 'F') {
         value += hex[x] - 'A' + 10;
      } else if(hex[x] >= 'a' && hex[x] <= 'f') {
         value += hex[x] - 'a' + 10;
      }
   }

   return value;

}

/** Compute a color lighter than the input. */
void LightenColor(ColorType oldColor, ColorType newColor)
{

   XColor temp;
   int red, green, blue;

   temp = GetXColorFromRGB(rgbColors[oldColor]);

   /* Convert to 0.0 to 1.0 in fixed point with 8 bits for the fraction. */
   red   = temp.red   >> 8;
   green = temp.green >> 8;
   blue  = temp.blue  >> 8;

   /* Multiply by 1.45 which is 371. */
   red   = (red   * 371) >> 8;
   green = (green * 371) >> 8;
   blue  = (blue  * 371) >> 8;

   /* Convert back to 0-65535. */
   red   |= red << 8;
   green |= green << 8;
   blue  |= blue << 8;

   /* Cap at 65535. */
   red   = Min(65535, red);
   green = Min(65535, green);
   blue  = Min(65535, blue);

   temp.red = red;
   temp.green = green;
   temp.blue = blue;

   GetColor(&temp);
   colors[newColor] = temp.pixel;
   rgbColors[newColor] = GetRGBFromXColor(&temp);

}

/** Compute a color darker than the input. */
void DarkenColor(ColorType oldColor, ColorType newColor)
{

   XColor temp;
   int red, green, blue;

   temp = GetXColorFromRGB(rgbColors[oldColor]);

   /* Convert to 0.0 to 1.0 in fixed point with 8 bits for the fraction. */
   red   = temp.red   >> 8;
   green = temp.green >> 8;
   blue  = temp.blue  >> 8;

   /* Multiply by 0.55 which is 141. */
   red   = (red   * 141) >> 8;
   green = (green * 141) >> 8;
   blue  = (blue  * 141) >> 8;

   /* Convert back to 0-65535. */
   red   |= red << 8;
   green |= green << 8;
   blue  |= blue << 8;

   temp.red = red;
   temp.green = green;
   temp.blue = blue;

   GetColor(&temp);
   colors[newColor] = temp.pixel;
   rgbColors[newColor] = GetRGBFromXColor(&temp);

}

/** Look up a color by name. */
int GetColorByName(const char *str, XColor *c)
{

   Assert(str);
   Assert(c);

   if(!JXParseColor(display, rootColormap, str, c)) {
      return 0;
   }

   GetColor(c);

   return 1;

}

/** Compute the RGB components from an index into our RGB colormap. */
void GetColorFromIndex(XColor *c)
{

   unsigned long red;
   unsigned long green;
   unsigned long blue;

   Assert(c);

   red = (c->pixel & redMask) << redShift;
   green = (c->pixel & greenMask) << greenShift;
   blue = (c->pixel & blueMask) << blueShift;

   c->red = red >> 16;
   c->green = green >> 16;
   c->blue = blue >> 16;

}

/** Compute the pixel value from RGB components. */
void GetDirectPixel(XColor *c)
{

   unsigned long red;
   unsigned long green;
   unsigned long blue;

   Assert(c);

   /* Normalize. */
   red = c->red << 16;
   green = c->green << 16;
   blue = c->blue << 16;

   /* Shift to the correct offsets and mask. */
   red = (red >> redShift) & redMask;
   green = (green >> greenShift) & greenMask;
   blue = (blue >> blueShift) & blueMask;

   /* Combine. */
   c->pixel = red | green | blue;

}

/** Compute the pixel value from RGB components. */
void GetMappedPixel(XColor *c)
{
   Assert(c);
   Assert(map);
   GetDirectPixel(c);
   c->pixel = map[c->pixel];
}

/** Compute the pixel value from RGB components. */
void GetColor(XColor *c)
{
   Assert(c);
   Assert(rootVisual);
   switch(rootVisual->class) {
   case DirectColor:
   case TrueColor:
      GetDirectPixel(c);
      return;
   default:
      GetMappedPixel(c);
      return;
   }
}

/** Get the RGB components from a pixel value. */
void GetColorFromPixel(XColor *c)
{

   Assert(c);
	switch(rootVisual->class) {
	case DirectColor:
	case TrueColor:
		/* Nothing to do. */
		break;
	default:
   	/* Convert from a pixel value to a linear RGB space. */
   	c->pixel = rmap[c->pixel & 255];
		break;
	}

   /* Extract the RGB components from the linear RGB pixel value. */
   GetColorFromIndex(c);

}


/** Get an RGB pixel value from RGB components. */
void GetColorIndex(XColor *c)
{
   Assert(c);
   GetDirectPixel(c);
}

/** Get an XFT color for the specified component. */
#ifdef USE_XFT
XftColor *GetXftColor(ColorType type)
{

   if(!xftColors[type]) {
      XRenderColor rcolor;
      const unsigned long rgb = rgbColors[type];
      xftColors[type] = Allocate(sizeof(XftColor));
      rcolor.alpha = 65535;
      rcolor.red = ((rgb >> 16) & 0xFF) * 257;
      rcolor.green = ((rgb >> 8) & 0xFF) * 257;
      rcolor.blue = (rgb & 0xFF) * 257;
      JXftColorAllocValue(display, rootVisual, rootColormap, &rcolor,
                          xftColors[type]);
   }

   return xftColors[type];

}
#endif

