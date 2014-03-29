/**
 * @file traybutton.h
 * @author Joe Wingbermuehle
 * @date 2004-2006
 *
 * @brief Button tray component.
 *
 */

#include "jwm.h"
#include "traybutton.h"
#include "tray.h"
#include "icon.h"
#include "image.h"
#include "error.h"
#include "root.h"
#include "main.h"
#include "color.h"
#include "font.h"
#include "button.h"
#include "misc.h"
#include "screen.h"
#include "desktop.h"
#include "popup.h"
#include "timing.h"
#include "command.h"
#include "cursor.h"
#include "settings.h"
#include "event.h"

#define BUTTON_SIZE 4

typedef struct TrayButtonType {

   TrayComponentType *cp;

   char *label;
   char *popup;
   char *iconName;
   IconNode *icon;
   char *action;
   char border;

   int mousex;
   int mousey;
   TimeType mouseTime;

   struct TrayButtonType *next;

} TrayButtonType;

static TrayButtonType *buttons = NULL;

static void Create(TrayComponentType *cp);
static void Destroy(TrayComponentType *cp);
static void SetSize(TrayComponentType *cp, int width, int height);
static void Resize(TrayComponentType *cp);
static void Draw(TrayComponentType *cp, int active);

static void ProcessButtonPress(TrayComponentType *cp,
                               int x, int y, int mask);
static void ProcessButtonRelease(TrayComponentType *cp,
                                 int x, int y, int mask);
static void ProcessMotionEvent(TrayComponentType *cp,
                               int x, int y, int mask);
static void SignalTrayButton(const TimeType *now,
                             int x, int y, Window w, void *data);

/** Startup tray buttons. */
void StartupTrayButtons(void)
{
   TrayButtonType *bp;
   for(bp = buttons; bp; bp = bp->next) {
      if(bp->label) {
         bp->cp->requestedWidth
            = GetStringWidth(FONT_TRAYBUTTON, bp->label) + 4;
         bp->cp->requestedHeight
            = GetStringHeight(FONT_TRAYBUTTON);
      } else {
         bp->cp->requestedWidth = 0;
         bp->cp->requestedHeight = 0;
      }
      if(bp->iconName) {
         bp->icon = LoadNamedIcon(bp->iconName, 1);
         if(JLIKELY(bp->icon)) {
            bp->cp->requestedWidth += bp->icon->image->width;
            bp->cp->requestedHeight += bp->icon->image->height;
         } else {
            Warning(_("could not load tray icon: \"%s\""), bp->iconName);
         }
      }
      bp->cp->requestedWidth += 2 * BUTTON_SIZE;
      bp->cp->requestedHeight += 2 * BUTTON_SIZE;
   }
}

/** Release tray button data. */
void DestroyTrayButtons(void)
{
   TrayButtonType *bp;
   while(buttons) {
      bp = buttons->next;
      UnregisterCallback(SignalTrayButton, buttons);
      if(buttons->label) {
         Release(buttons->label);
      }
      if(buttons->iconName) {
         Release(buttons->iconName);
      }
      if(buttons->action) {
         Release(buttons->action);
      }
      if(buttons->popup) {
         Release(buttons->popup);
      }
      Release(buttons);
      buttons = bp;
   }
}

/** Create a button tray component. */
TrayComponentType *CreateTrayButton(const char *iconName,
                                    const char *label,
                                    const char *action,
                                    const char *popup,
                                    unsigned int width,
                                    unsigned int height,
                                    char border)
{

   TrayButtonType *bp;
   TrayComponentType *cp;

   if(JUNLIKELY((label == NULL || strlen(label) == 0)
      && (iconName == NULL || strlen(iconName) == 0))) {
      Warning(_("no icon or label for TrayButton"));
      return NULL;
   }

   bp = Allocate(sizeof(TrayButtonType));
   bp->next = buttons;
   buttons = bp;

   bp->icon = NULL;
   bp->iconName = CopyString(iconName);
   bp->label = CopyString(label);
   bp->action = CopyString(action);
   bp->popup = CopyString(popup);
   bp->border = border;

   cp = CreateTrayComponent();
   cp->object = bp;
   bp->cp = cp;
   cp->requestedWidth = width;
   cp->requestedHeight = height;

   bp->mousex = -settings.doubleClickDelta;
   bp->mousey = -settings.doubleClickDelta;

   cp->Create = Create;
   cp->Destroy = Destroy;
   cp->SetSize = SetSize;
   cp->Resize = Resize;

   cp->ProcessButtonPress = ProcessButtonPress;
   cp->ProcessButtonRelease = ProcessButtonRelease;
   if(popup || label) {
      cp->ProcessMotionEvent = ProcessMotionEvent;
   }

   RegisterCallback(settings.popupDelay / 2, SignalTrayButton, bp);

   return cp;

}

/** Set the size of a button tray component. */
void SetSize(TrayComponentType *cp, int width, int height)
{

   TrayButtonType *bp;

   bp = (TrayButtonType*)cp->object;

   if(bp->icon) {

      int labelWidth, labelHeight;
      int iconWidth, iconHeight;
      int ratio;

      if(bp->label) {
         labelWidth = GetStringWidth(FONT_TRAYBUTTON, bp->label) + 4;
         labelHeight = GetStringHeight(FONT_TRAYBUTTON);
      } else {
         labelWidth = 0;
         labelHeight = 0;
      }

      iconWidth = bp->icon->image->width;
      iconHeight = bp->icon->image->height;

      /* Fixed point with 16 bit fraction. */
      ratio = (iconWidth << 16) / iconHeight;

      if(width > 0) {

         /* Compute height from width. */
         iconWidth = width - labelWidth - 2 * BUTTON_SIZE;
         iconHeight = (iconWidth << 16) / ratio;
         height = Max(iconHeight, labelHeight) + 2 * BUTTON_SIZE;

      } else if(height > 0) {

         /* Compute width from height. */
         iconHeight = height - 2 * BUTTON_SIZE;
         iconWidth = (iconHeight * ratio) >> 16;
         width = iconWidth + labelWidth + 2 * BUTTON_SIZE;

      }

      cp->width = width;
      cp->height = height;

   }

}

/** Initialize a button tray component. */
void Create(TrayComponentType *cp)
{

   TrayButtonType *bp;

   bp = (TrayButtonType*)cp->object;

   /* Validate the action for this tray button. */
   if(bp->action && strlen(bp->action) > 0) {
      if(!strncmp(bp->action, "exec:", 5)) {
         /* Valid. */
      } else if(!strncmp(bp->action, "root:", 5)) {
         /* Valid. However, the specified root menu may not exist.
          * This case is handled in ValidateTrayButtons.
          */
      } else if(!strcmp(bp->action, "showdesktop")) {
         /* Valid. */
      } else {
         Warning(_("invalid TrayButton action: \"%s\""), bp->action);
      }
   } else {
      /* Valid. However, root menu 1 may not exist.
       * This case is handled in ValidateTrayButtons.
       */
   }

   cp->pixmap = JXCreatePixmap(display, rootWindow,
                               cp->width, cp->height, rootDepth);

   Draw(cp, 0);

}

/** Resize a button tray component. */
void Resize(TrayComponentType *cp)
{
   Destroy(cp);
   Create(cp);
}

/** Destroy a button tray component. */
void Destroy(TrayComponentType *cp)
{
   if(cp->pixmap != None) {
      JXFreePixmap(display, cp->pixmap);
   }
}

/** Draw a tray button. */
void Draw(TrayComponentType *cp, int active)
{

   ButtonNode button;
   TrayButtonType *bp;

   bp = (TrayButtonType*)cp->object;

   ClearTrayDrawable(cp);
   ResetButton(&button, cp->pixmap, rootGC);
   if(active) {
      button.type = BUTTON_TRAY_ACTIVE;
   } else {
      button.border = bp->border;
      button.type = BUTTON_TRAY;
   }
   button.width = cp->width;
   button.height = cp->height;
   button.x = 0;
   button.y = 0;
   button.font = FONT_TRAYBUTTON;
   button.text = bp->label;
   button.icon = bp->icon;
   DrawButton(&button);

}

/** Process a button press. */
void ProcessButtonPress(TrayComponentType *cp, int x, int y, int mask)
{

   const ScreenType *sp;
   int mwidth, mheight;
   int button;

   TrayButtonType *bp = (TrayButtonType*)cp->object;

   Assert(bp);

   if(bp->action && strlen(bp->action) > 0) {
      if(strncmp(bp->action, "root:", 5)) {
         GrabMouse(cp->tray->window);
         cp->grabbed = 1;
         Draw(cp, 1);
         UpdateSpecificTray(cp->tray, cp);
         return;
      } else {
         button = atoi(bp->action + 5);
      }
   } else {
      button = 1;
   }

   GetRootMenuSize(button, &mwidth, &mheight);

   sp = GetCurrentScreen(cp->screenx, cp->screeny);

   if(cp->tray->layout == LAYOUT_HORIZONTAL) {
      x = cp->screenx;
      if(cp->screeny + cp->height / 2 < sp->y + sp->height / 2) {
         y = cp->screeny + cp->height;
      } else {
         y = cp->screeny - mheight;
      }
   } else {
      y = cp->screeny;
      if(cp->screenx + cp->width / 2 < sp->x + sp->width / 2) {
         x = cp->screenx + cp->width;
      } else {
         x = cp->screenx - mwidth;
      }
   }

   Draw(cp, 1);
   UpdateSpecificTray(cp->tray, cp);
   ShowRootMenu(button, x, y);
   Draw(cp, 0);
   UpdateSpecificTray(cp->tray, cp);

}

/** Process a button release. */
void ProcessButtonRelease(TrayComponentType *cp, int x, int y, int mask)
{

   TrayButtonType *bp = (TrayButtonType*)cp->object;

   Assert(bp);

   Draw(cp, 0);
   UpdateSpecificTray(cp->tray, cp);

   // Since we grab the mouse, make sure the mouse is actually
   // over the button.
   if(x < 0 || x >= cp->width) {
      return;
   }
   if(y < 0 || y >= cp->height) {
      return;
   }

   // Run the tray button action (if any).
   if(bp->action && strlen(bp->action) > 0) {
      if(!strncmp(bp->action, "exec:", 5)) {
         RunCommand(bp->action + 5);
         return;
      } else if(!strcmp(bp->action, "showdesktop")) {
         ShowDesktop();
         return;
      }
   }

}

/** Process a motion event. */
void ProcessMotionEvent(TrayComponentType *cp, int x, int y, int mask)
{
   TrayButtonType *bp = (TrayButtonType*)cp->object;
   bp->mousex = cp->screenx + x;
   bp->mousey = cp->screeny + y;
   GetCurrentTime(&bp->mouseTime);
}

/** Signal (needed for popups). */
void SignalTrayButton(const TimeType *now, int x, int y, Window w, void *data)
{
   TrayButtonType *bp = (TrayButtonType*)data;
   const char *popup;

   if(bp->popup) {
      popup = bp->popup;
   } else if(bp->label) {
      popup = bp->label;
   } else {
      return;
   }
   if(bp->cp->tray->window == w &&
      abs(bp->mousex - x) < settings.doubleClickDelta &&
      abs(bp->mousey - y) < settings.doubleClickDelta) {
      if(GetTimeDifference(now, &bp->mouseTime) >= settings.popupDelay) {
         ShowPopup(x, y, popup);
      }
   }
}

/** Validate tray buttons. */
void ValidateTrayButtons(void)
{
   TrayButtonType *bp;
   int bindex;
   for(bp = buttons; bp; bp = bp->next) {
      if(bp->action && !strncmp(bp->action, "root:", 5)) {
         bindex = atoi(bp->action + 5);
         if(JUNLIKELY(!IsRootMenuDefined(bindex))) {
            Warning(_("tray button: root menu %d not defined"), bindex);
         }
      }
   }
}

