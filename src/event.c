/****************************************************************************
 * Functions to handle XServer events.
 * Copyright (C) 2004 Joe Wingbermuehle
 ****************************************************************************/

#include "jwm.h"
#include "event.h"
#include "client.h"
#include "main.h"
#include "hint.h"
#include "tray.h"
#include "pager.h"
#include "desktop.h"
#include "cursor.h"
#include "icon.h"
#include "taskbar.h"
#include "confirm.h"
#include "swallow.h"
#include "popup.h"
#include "winmenu.h"
#include "root.h"
#include "move.h"
#include "resize.h"
#include "key.h"

static int showMenuOnRoot = 1;

static void DispatchBorderButtonEvent(const XButtonEvent *event,
	ClientNode *np);

static void HandleConfigureRequest(const XConfigureRequestEvent *event);
static int HandleExpose(const XExposeEvent *event);
static int HandlePropertyNotify(const XPropertyEvent *event);
static void HandleConfigureNotify(const XConfigureEvent *event);
static void HandleClientMessage(const XClientMessageEvent *event);
static void HandleColormapChange(const XColormapEvent *event);
static int HandleDestroyNotify(const XDestroyWindowEvent *event);
static void HandleMapRequest(const XMapEvent *event);
static void HandleUnmapNotify(const XUnmapEvent *event);
static void HandleButtonEvent(const XButtonEvent *event);
static void HandleKeyPress(const XKeyEvent *event);
static void HandleEnterNotify(const XCrossingEvent *event);
static void HandleLeaveNotify(const XCrossingEvent *event);
static void HandleMotionNotify(const XMotionEvent *event);

#ifdef USE_SHAPE
static void HandleShapeEvent(const XShapeEvent *event);
#endif

/****************************************************************************
 ****************************************************************************/
void WaitForEvent(XEvent *event) {

	struct timeval timeout;
	fd_set fds;
	int fd;
	int handled;

	fd = JXConnectionNumber(display);

	do {

		while(JXPending(display) == 0) {
			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			timeout.tv_usec = 0;
			timeout.tv_sec = 1;
			if(select(fd + 1, &fds, NULL, NULL, &timeout) <= 0) {
				SignalTaskbar();
			}
		}

		SignalTaskbar();

		JXNextEvent(display, event);

		switch(event->type) {
		case ConfigureRequest:
			HandleConfigureRequest(&event->xconfigurerequest);
			handled = 1;
			break;
		case MapRequest:
			HandleMapRequest(&event->xmap);
			handled = 1;
			break;
		case PropertyNotify:
			handled = HandlePropertyNotify(&event->xproperty);
			break;
		case ClientMessage:
			HandleClientMessage(&event->xclient);
			handled = 1;
			break;
		case UnmapNotify:
			HandleUnmapNotify(&event->xunmap);
			handled = 1;
			break;
		case Expose:
			handled = HandleExpose(&event->xexpose);
			break;
		case ColormapNotify:
			HandleColormapChange(&event->xcolormap);
			handled = 1;
			break;
		case DestroyNotify:
			handled = HandleDestroyNotify(&event->xdestroywindow);
			break;
		case ConfigureNotify:
			HandleConfigureNotify(&event->xconfigure);
			handled = 1;
			break;
		case CreateNotify:
		case MapNotify:
		case NoExpose:
		case ReparentNotify:
		case GraphicsExpose:
			handled = 1;
			break;
		default:
#ifdef USE_SHAPE
			if(haveShape && event->type == shapeEvent) {
				HandleShapeEvent((XShapeEvent*)(void*)&event);
				handled = 1;
			} else {
				handled = 0;
			}
#else
			handled = 0;
#endif
			break;
		}

		if(!handled) {
			handled = ProcessTrayEvent(event);
		}
		if(!handled) {
			handled = ProcessDialogEvent(event);
		}
		if(!handled) {
			handled = ProcessSwallowEvent(event);
		}

		handled |= ProcessPopupEvent(event);

	} while(handled && !shouldExit);

}

/****************************************************************************
 ****************************************************************************/
void ProcessEvent(XEvent *event) {

	switch(event->type) {
	case ButtonPress:
	case ButtonRelease:
		HandleButtonEvent(&event->xbutton);
		break;
	case KeyPress:
		HandleKeyPress(&event->xkey);
		break;
	case EnterNotify:
		HandleEnterNotify(&event->xcrossing);
		break;
	case LeaveNotify:
		HandleLeaveNotify(&event->xcrossing);
		break;
	case MotionNotify:
		while(JXCheckTypedEvent(display, MotionNotify, event));
		HandleMotionNotify(&event->xmotion);
		break;
	case DestroyNotify:
	case Expose:
	case KeyRelease:
		break;
	default:
		Debug("Unknown event type: %d", event->type);
		break;
	}
}

/****************************************************************************
 ****************************************************************************/
void HandleButtonEvent(const XButtonEvent *event) {
	int x, y;
	ClientNode *np;

	np = FindClientByParent(event->window);
	if(np) {
		RaiseClient(np);
		if(focusModel == FOCUS_CLICK) {
			FocusClient(np);
		}
		switch(event->button) {
		case Button1:
			DispatchBorderButtonEvent(event, np);
			break;
		case Button2:
			MoveClient(np, event->x, event->y);
			break;
		case Button3:
			x = event->x + np->x;
			y = event->y + np->y;
			if(np->state.border & BORDER_OUTLINE) {
				x -= borderWidth;
				if(np->state.border & BORDER_TITLE) {
					y -= titleSize;
				} else {
					y -= borderWidth;
				}
			} else {
				if(np->state.border & BORDER_TITLE) {
					y -= titleSize;
				}
			}
			ShowWindowMenu(np, x, y);
			break;
		default:
			break;
		}
	} else if(event->window == rootWindow) {
		if(showMenuOnRoot) {
			ShowRootMenu(event->x, event->y);
		}
	} else {
		np = FindClientByWindow(event->window);
		if(np) {
			switch(event->button) {
			case Button1:
			case Button2:
			case Button3:
				RaiseClient(np);
				if(focusModel == FOCUS_CLICK) {
					FocusClient(np);
				}
				break;
			default:
				break;
			}
			JXAllowEvents(display, ReplayPointer, CurrentTime);
		}
	}

	UpdatePager();
}

/****************************************************************************
 ****************************************************************************/
void HandleKeyPress(const XKeyEvent *event) {
	ClientNode *np;
	KeyType key;

	key = GetKey(event);

	if(focusModel == FOCUS_CLICK) {
		np = FindClientByWindow(event->subwindow);
	} else {
		np = GetActiveClient();
	}

	switch(key & 0xFF) {
	case KEY_EXEC:
		RunKeyCommand(event);
		break;
	case KEY_DESKTOP:
		if(key >> 8) {
			ChangeDesktop((key >> 8) - 1);
		} else {
			NextDesktop();
		}
		break;
	case KEY_NEXT:
		FocusNext();
		break;
	case KEY_CLOSE:
		if(np) {
			DeleteClient(np);
		}
		break;
	case KEY_SHADE:
		if(np) {
			if(np->state.status & STAT_SHADED) {
				UnshadeClient(np);
			} else {
				ShadeClient(np);
			}
		}
		break;
	case KEY_MOVE:
		if(np) {
			MoveClientKeyboard(np);
		}
		break;
	case KEY_RESIZE:
		if(np) {
			ResizeClientKeyboard(np);
		}
		break;
	case KEY_MIN:
		if(np) {
			MinimizeClient(np);
		}
		break;
	case KEY_MAX:
		if(np) {
			MaximizeClient(np);
		}
		break;
	case KEY_ROOT:
		ShowRootMenu(0, 0);
		break;
	case KEY_WIN:
		if(np) {
			ShowWindowMenu(np, np->x, np->y);
		}
		break;
	case KEY_RESTART:
		Restart();
		break;
	case KEY_EXIT:
		Exit();
		break;
	default:
		break;
	}

}

/****************************************************************************
 ****************************************************************************/
void HandleConfigureRequest(const XConfigureRequestEvent *event) {
	XWindowChanges wc;
	ClientNode *np;
	int north, south, east, west;
	int changed;

	np = FindClientByWindow(event->window);
	if(np && np->window == event->window) {

		if(np->controller) {
			(np->controller)(0);
		}

		changed = 0;
		if((event->value_mask & CWWidth) && (event->width != np->width)) {
			np->width = event->width;
			changed = 1;
		}
		if((event->value_mask & CWHeight) && (event->height != np->height)) {
			np->height = event->height;
			changed = 1;
		}
		if((event->value_mask & CWX) && (event->x != np->x)) {
			np->x = event->x;
			changed = 1;
		}
		if((event->value_mask & CWY) && (event->y != np->y)) {
			np->y = event->y;
			changed = 1;
		}

		if(!changed) {
			return;
		}

		if(np->state.border & BORDER_OUTLINE) {
			east = borderWidth;
			west = borderWidth;
			south = borderWidth;
			if(np->state.border & BORDER_TITLE) {
				north = titleSize;
			} else {
				north = borderWidth;
			}
		} else {
			north = 0;
			south = 0;
			east = 0;
			west = 0;
		}

		wc.stack_mode = Above;
		wc.sibling = np->parent;
		wc.border_width = 0;

		wc.x = np->x;
		wc.y = np->y;
		wc.width = np->width + east + west;
		wc.height = np->height + north + south;
		JXConfigureWindow(display, np->parent, event->value_mask, &wc);

		wc.x = west;
		wc.y = north;
		wc.width = np->width;
		wc.height = np->height;
		JXConfigureWindow(display, np->window, event->value_mask, &wc);

	} else {
		wc.stack_mode = event->detail;
		wc.sibling = event->above;
		wc.border_width = event->border_width;
		wc.x = event->x;
		wc.y = event->y;
		wc.width = event->width;
		wc.height = event->height;
		JXConfigureWindow(display, event->window, event->value_mask, &wc);
	}

}

/****************************************************************************
 ****************************************************************************/
void HandleEnterNotify(const XCrossingEvent *event) {
	ClientNode *np;
	Cursor cur;

	np = FindClientByWindow(event->window);
	if(np) {
		if(!(np->state.status & STAT_ACTIVE) && (focusModel == FOCUS_SLOPPY)) {
			FocusClient(np);
		}
		if(np->parent == event->window) {
			np->borderAction = GetBorderActionType(np, event->x, event->y);
			cur = GetFrameCursor(np->borderAction);
			JXDefineCursor(display, np->parent, cur);
		} else if(np->borderAction != BA_NONE) {
			SetDefaultCursor(np->parent);
			np->borderAction = BA_NONE;
		}
	}
}

/****************************************************************************
 ****************************************************************************/
void HandleLeaveNotify(const XCrossingEvent *event) {
	ClientNode *np;

	np = FindClientByParent(event->window);
	if(np) {
		SetDefaultCursor(np->parent);
	}
}

/****************************************************************************
 ****************************************************************************/
int HandleExpose(const XExposeEvent *event) {
	ClientNode *np;

	if(event->count) {
		return 1;
	}

	np = FindClientByWindow(event->window);
	if(np) {
		if(event->window == np->parent) {
			DrawBorder(np);
			return 1;
		} else if(event->window == np->window
			&& np->state.status & STAT_WMDIALOG) {
			return 0;
		} else {
			return 1;
		}
	} else {
		return 0;
	}
}

/****************************************************************************
 ****************************************************************************/
int HandlePropertyNotify(const XPropertyEvent *event) {

	ClientNode *np;
	int changed;

	np = FindClientByWindow(event->window);
	if(np) {
		changed = 0;
		switch(event->atom) {
		case XA_WM_NAME:
			ReadWMName(np);
			changed = 1;
			break;
		case XA_WM_NORMAL_HINTS:
			ReadWMNormalHints(np);
			changed = 1;
			break;
		case XA_WM_HINTS:
		case XA_WM_ICON_NAME:
		case XA_WM_CLIENT_MACHINE:
			break;
		default:
			if(event->atom == atoms[ATOM_WM_COLORMAP_WINDOWS]) {
				ReadWMColormaps(np);
				UpdateClientColormap(np);
			} else if(event->atom == atoms[ATOM_NET_WM_ICON]) {
				LoadIcon(np);
				changed = 1;
			} else if(event->atom == atoms[ATOM_NET_WM_NAME]) {
				ReadWMName(np);
				changed = 1;
			}
			break;
		}

		if(changed) {
			DrawBorder(np);
			UpdateTaskBar();
			UpdatePager();
		}
		if(np->state.status & STAT_WMDIALOG) {
			return 0;
		} else {
			return 1;
		}
	} else if(event->window == rootWindow) {
		if(event->atom == atoms[ATOM_JWM_RESTART]) {
			Restart();
		} else if(event->atom == atoms[ATOM_JWM_EXIT]) {
			Exit();
		}
	}

	return 1;
}

/****************************************************************************
 ****************************************************************************/
void HandleConfigureNotify(const XConfigureEvent *event) {
#ifdef USE_SHAPE
	ClientNode *np;

	np = FindClientByWindow(event->window);
	if(np && (np->state.status & STAT_USESHAPE)) {
		SetShape(np);
	}
#endif
}

/****************************************************************************
 ****************************************************************************/
void HandleClientMessage(const XClientMessageEvent *event) {

	ClientNode *np;
	long mask, flags;
	char *atomName;
	int actionMaximize;
	int actionStick;
	int actionShade;
	int x;

	np = FindClientByWindow(event->window);
	if(np) {
		if(event->message_type == atoms[ATOM_WIN_STATE]) {

			mask = event->data.l[0];
			flags = event->data.l[1];

			if(mask & WIN_STATE_STICKY) {
				if(flags & WIN_STATE_STICKY) {
					SetClientSticky(np, 1);
				} else {
					SetClientSticky(np, 0);
				}
			}

			if(mask & WIN_STATE_HIDDEN) {
				if(flags & WIN_STATE_HIDDEN) {
					np->state.status |= STAT_NOLIST;
				} else {
					np->state.status &= ~STAT_NOLIST;
				}
				UpdateTaskBar();
				UpdatePager();
			}

		} else if(event->message_type == atoms[ATOM_WIN_LAYER]) {

			SetClientLayer(np, event->data.l[0]);

		} else if(event->message_type == atoms[ATOM_WM_CHANGE_STATE]) {

			if(np->controller) {
				(np->controller)(0);
			}

			switch(event->data.l[0]) {
			case WithdrawnState:
				SetClientWithdrawn(np, 1);
				break;
			case IconicState:
				MinimizeClient(np);
				break;
			case NormalState:
				RestoreClient(np);
				break;
			default:
				break;
			}

		} else if(event->message_type == atoms[ATOM_NET_ACTIVE_WINDOW]) {

			RestoreClient(np);
			FocusClient(np);

		} else if(event->message_type == atoms[ATOM_NET_WM_DESKTOP]) {

			if(event->data.l[0] == ~0UL) {
				SetClientSticky(np, 1);
			} else {

				if(np->controller) {
					(np->controller)(0);
				}

				if(event->data.l[0] >= 0 && event->data.l[0] < desktopCount) {
					np->state.status &= ~STAT_STICKY;
					SetClientDesktop(np, event->data.l[0]);
				}
			}

		} else if(event->message_type == atoms[ATOM_NET_CLOSE_WINDOW]) {

			DeleteClient(np);

		} else if(event->message_type == atoms[ATOM_NET_WM_STATE]) {

			/* Up to two actions to be applied together, figure it out. */
			actionMaximize = 0;
			actionStick = 0;
			actionShade = 0;

			for(x = 1; x <= 2; x++) {
				if(event->data.l[x] == atoms[ATOM_NET_WM_STATE_STICKY]) {
					actionStick = 1;
				} else if(event->data.l[x]
					== atoms[ATOM_NET_WM_STATE_MAXIMIZED_VERT]) {
					actionMaximize = 1;
				} else if(event->data.l[x]
					== atoms[ATOM_NET_WM_STATE_MAXIMIZED_HORZ]) {
					actionMaximize = 1;
				} else if(event->data.l[x] == atoms[ATOM_NET_WM_STATE_SHADED]) {
					actionShade = 1;
				}
			}

			switch(event->data.l[0]) {
			case 0: /* Remove */
				if(actionStick) {
					SetClientSticky(np, 0);
				}
				if(actionMaximize && (np->state.status & STAT_MAXIMIZED)) {
					MaximizeClient(np);
				}
				if(actionShade) {
					UnshadeClient(np);
				}
				break;
			case 1: /* Add */
				if(actionStick) {
					SetClientSticky(np, 1);
				}
				if(actionMaximize && !(np->state.status & STAT_MAXIMIZED)) {
					MaximizeClient(np);
				}
				if(actionShade) {
					ShadeClient(np);
				}
				break;
			case 2: /* Toggle */
				if(actionStick) {
					if(np->state.status & STAT_STICKY) {
						SetClientSticky(np, 0);
					} else {
						SetClientSticky(np, 1);
					}
				}
				if(actionMaximize) {
					MaximizeClient(np);
				}
				if(actionShade) {
					if(np->state.status & STAT_SHADED) {
						UnshadeClient(np);
					} else {
						ShadeClient(np);
					}
				}
				break;
			default:
				Debug("bad _NET_WM_STATE action: %ld", event->data.l[0]);
				break;
			}

		} else {
			atomName = JXGetAtomName(display, event->message_type);
			Debug("Uknown ClientMessage: %s", atomName);
			JXFree(atomName);
		}
	}

}

/****************************************************************************
 ****************************************************************************/
void HandleMotionNotify(const XMotionEvent *event) {
	ClientNode *np;
	Cursor cur;
	BorderActionType action;

	if(event->is_hint) {
		return;
	}

	np = FindClientByParent(event->window);
	if(np && (np->state.border & BORDER_OUTLINE)) {
		action = GetBorderActionType(np, event->x, event->y);
		if(np->borderAction != action) {
			np->borderAction = action;
			cur = GetFrameCursor(action);
			JXDefineCursor(display, np->parent, cur);
		}
	}

}

/****************************************************************************
 ****************************************************************************/
#ifdef USE_SHAPE
void HandleShapeEvent(const XShapeEvent *event) {
	ClientNode *np;

	np = FindClientByWindow(event->window);
	if(np) {
		SetShape(np);
	}
}
#endif /* USE_SHAPE */

/****************************************************************************
 ****************************************************************************/
void HandleColormapChange(const XColormapEvent *event) {
	ClientNode *np;

	if(event->new == True) {
		np = FindClientByWindow(event->window);
		if(np) {
			np->cmap = event->colormap;
			UpdateClientColormap(np);
		}
	}
}

/****************************************************************************
 ****************************************************************************/
void HandleMapRequest(const XMapEvent *event) {
	ClientNode *np;

	Assert(event);

	np = FindClientByWindow(event->window);
	if(!np) {
		JXSync(display, False);
		JXGrabServer(display);
		np = AddClientWindow(event->window, 0, 1);
		if(np) {
			if(focusModel == FOCUS_CLICK) {
				FocusClient(np);
			}
		} else {
			JXMapWindow(display, event->window);
		}
		JXUngrabServer(display);
		JXSync(display, False);
	} else {
		if(!(np->state.status & STAT_MAPPED)) {
			np->state.status |= STAT_MAPPED;
			np->state.status &= ~STAT_MINIMIZED;
			np->state.status &= ~STAT_WITHDRAWN;
			JXMapWindow(display, np->window);
			JXMapWindow(display, np->parent);
			RaiseClient(np);
			if(focusModel == FOCUS_CLICK) {
				FocusClient(np);
			}
			UpdateTaskBar();
			UpdatePager();
		}
	}
	RestackClients();
}

/****************************************************************************
 ****************************************************************************/
void HandleUnmapNotify(const XUnmapEvent *event) {
	ClientNode *np;

	Assert(event);

	np = FindClientByWindow(event->window);
	if(np && np->window == event->window) {

		if(np->controller) {
			(np->controller)(1);
		}

		if(np->state.status & STAT_MAPPED) {
			np->state.status &= ~STAT_MAPPED;
			JXUnmapWindow(display, np->parent);
		}

	}

}

/****************************************************************************
 ****************************************************************************/
int HandleDestroyNotify(const XDestroyWindowEvent *event) {
	ClientNode *np;

	np = FindClientByWindow(event->window);
	if(np && np->window == event->window) {

		if(np->controller) {
			(np->controller)(1);
		}

		RemoveClient(np);

		return 1;

	}

	return 0;

}

/****************************************************************************
 ****************************************************************************/
void DispatchBorderButtonEvent(const XButtonEvent *event, ClientNode *np) {
	static Time lastClickTime = 0;
	static int lastX = 0, lastY = 0;
	static int doubleClickActive = 0;
	BorderActionType action;

	if(!(np->state.border & BORDER_OUTLINE)) {
		return;
	}

	action = GetBorderActionType(np, event->x, event->y);

	switch(action & 0x0F) {
	case BA_RESIZE:
		if(event->type == ButtonPress) {
			ResizeClient(np, action, event->x, event->y);
		}
		break;
	case BA_MOVE:
		if(event->type == ButtonPress) {
			if(doubleClickActive
				&& abs(event->time - lastClickTime) <= doubleClickSpeed
				&& abs(event->x - lastX) <= doubleClickDelta
				&& abs(event->y - lastY) <= doubleClickDelta) {
				if(np->state.status & STAT_SHADED) {
					UnshadeClient(np);
				} else {
					ShadeClient(np);
				}
				doubleClickActive = 0;
			} else {
				if(MoveClient(np, event->x, event->y)) {
					doubleClickActive = 0;
				} else {
					doubleClickActive = 1;
					lastClickTime = event->time;
					lastX = event->x;
					lastY = event->y;
				}
			}
		}
		break;
	case BA_CLOSE:
		if(event->type == ButtonRelease) {
			DeleteClient(np);
		}
		break;
	case BA_MAXIMIZE:
		if(event->type == ButtonRelease) {
			MaximizeClient(np);
		}
		break;
	case BA_MINIMIZE:
		if(event->type == ButtonRelease) {
			MinimizeClient(np);
		}
		break;
	default:
		break;
	}

}

/****************************************************************************
 ****************************************************************************/
void SetShowMenuOnRoot(int v) {
	showMenuOnRoot = v;
}


