//this file is part of docking functionality for Notepad++
//Copyright (C)2006 Jens Lorenz <jens.plugin.npp@gmx.de>
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "dockingResource.h"
#include "math.h"
#include "Docking.h"
#include "DockingCont.h"
#include "DropData.h"
#include "SplitterContainer.h"
#include "WindowInterface.h"
#include "SysMsg.h"
#include <Commctrl.h>
#include <shlobj.h>
#include "common_func.h"

#ifndef WH_MOUSE_LL
#define WH_MOUSE_LL 14
#endif

static HWND		hWndServer		= NULL;
static HHOOK	hookMouse		= NULL;

static LRESULT CALLBACK hookProcMouse(UINT nCode, WPARAM wParam, LPARAM lParam)
{
    if(nCode < 0)
    {
		::CallNextHookEx(hookMouse, nCode, wParam, lParam);
        return 0;
    }

    switch (wParam)
    {
		case WM_MOUSEMOVE:
		case WM_NCMOUSEMOVE:
			::PostMessage(hWndServer, wParam, 0, 0);
			break;
		case WM_LBUTTONUP:
		case WM_NCLBUTTONUP:
			::PostMessage(hWndServer, wParam, 0, 0);
			break;
        default: 
			break;
	}

	return ::CallNextHookEx(hookMouse, nCode, wParam, lParam);
}


DockingCont::DockingCont(void)
{
	_isMouseOver		= FALSE;
	_isMouseClose		= FALSE;
	_isMouseDown		= FALSE;
	_isFloating			= false;
	_isTopCaption		= CAPTION_TOP;
	_dragFromTab		= FALSE;
	_hContTab			= NULL;
	_hDefaultTabProc	= NULL;
	_beginDrag			= FALSE;
	_prevItem			= 0;
	_hFont				= NULL;
	_vTbData.clear();
}

DockingCont::~DockingCont()
{
	::DeleteObject(_hFont);
}


void DockingCont::doDialog(bool willBeShown, bool isFloating)
{
	if (!isCreated())
	{
		create(IDD_CONTAINER_DLG);

		_isFloating  = isFloating;

		if (_isFloating == true)
		{
			::SetWindowLong(_hSelf, GWL_STYLE, POPUP_STYLES);
			::SetWindowLong(_hSelf, GWL_EXSTYLE, POPUP_EXSTYLES);
			::ShowWindow(_hCaption, SW_HIDE);
		}
		else
		{
			::SetWindowLong(_hSelf, GWL_STYLE, CHILD_STYLES);
			::SetWindowLong(_hSelf, GWL_EXSTYLE, CHILD_EXSTYLES);
			::ShowWindow(_hCaption, SW_SHOW);
		}

		_hFont = ::CreateFont(14, 0, 0, 0,
				 FW_NORMAL, FALSE, FALSE, FALSE,
				 ANSI_CHARSET, OUT_DEFAULT_PRECIS,
				 CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
				 DEFAULT_PITCH | FF_ROMAN,
				 "MS Shell Dlg");
	}

	display(willBeShown);
}


tTbData* DockingCont::createToolbar(tTbData data, Window **ppWin)
{
	tTbData *pTbData = new tTbData;

	*pTbData = data;

	/* force window style of client window */
	::SetWindowLong(pTbData->hClient, GWL_STYLE, CHILD_STYLES);
	::SetWindowLong(pTbData->hClient, GWL_EXSTYLE, CHILD_EXSTYLES);

	/* restore position if plugin is in floating state */
	if ((_isFloating == true) && (::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0) == 0))
	{
		reSizeToWH(pTbData->rcFloat);
	}

	/* set attached child window */
    ::SetParent(pTbData->hClient, ::GetDlgItem(_hSelf, IDC_CLIENT_TAB));

	/* set names for captions and view toolbar */
	viewToolbar(pTbData);

	/* attach to list */
	_vTbData.push_back(pTbData);

	return pTbData;
}


tTbData DockingCont::destroyToolbar(tTbData TbData)
{
	int			iItemCnt	= 0;

	/* remove from list */
	for (size_t iTb = 0; iTb < _vTbData.size(); iTb++)
	{
		if (_vTbData[iTb]->hClient == TbData.hClient)
		{
			/* remove tab */
			hideToolbar(_vTbData[iTb]);

			/* release client from container */
			::SetParent(TbData.hClient, NULL);

			/* free resources */
			delete _vTbData[iTb];
			vector<tTbData*>::iterator itr = _vTbData.begin() + iTb;
			_vTbData.erase(itr);
		}
	}
	return TbData;
}


tTbData* DockingCont::findToolbarByWnd(HWND hClient)
{
	tTbData*	pTbData		= NULL;

	/* find entry by handle */
	for (size_t iTb = 0; iTb < _vTbData.size(); iTb++)
	{
		if (hClient == _vTbData[iTb]->hClient)
		{
			pTbData = _vTbData[iTb];
		}
	}
	return pTbData;
}

tTbData* DockingCont::findToolbarByName(char* pszName)
{
	tTbData*	pTbData		= NULL;

	/* find entry by handle */
	for (size_t iTb = 0; iTb < _vTbData.size(); iTb++)
	{
		if (strcmp(pszName, _vTbData[iTb]->pszName) == 0)
		{
			pTbData = _vTbData[iTb];
		}
	}
	return pTbData;
}

void DockingCont::setActiveTb(tTbData* pTbData)
{
	int iItem = SearchPosInTab(pTbData);
	setActiveTb(iItem);
}

void DockingCont::setActiveTb(int iItem)
{
	//if ((iItem != -1) && (iItem < ::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0)))
	if (iItem < ::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0))
	{
		SelectTab(iItem);
	}
}

int DockingCont::getActiveTb(void)
{
	return ::SendMessage(_hContTab, TCM_GETCURSEL, 0, 0);
}

tTbData* DockingCont::getDataOfActiveTb(void)
{
	tTbData*	pTbData	= NULL;
	int			iItem	= getActiveTb();

	if (iItem != -1)
	{
		TCITEM	tcItem	= {0};

		tcItem.mask		= TCIF_PARAM;
		::SendMessage(_hContTab, TCM_GETITEM, iItem, (LPARAM)&tcItem);
		pTbData = (tTbData*)tcItem.lParam;
	}

	return pTbData;
}

vector<tTbData*> DockingCont::getDataOfVisTb(void)
{
	vector<tTbData*>	vTbData;
	TCITEM				tcItem		= {0};
	int					iItemCnt	= ::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0);

	tcItem.mask	= TCIF_PARAM;

	for(int iItem = 0; iItem < iItemCnt; iItem++)
	{
		::SendMessage(_hContTab, TCM_GETITEM, iItem, (LPARAM)&tcItem);
		vTbData.push_back((tTbData*)tcItem.lParam);
	}
	return vTbData;
}

bool DockingCont::isTbVis(tTbData* data)
{
	bool				bRet		= false;
	TCITEM				tcItem		= {0};
	int					iItemCnt	= ::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0);

	tcItem.mask	= TCIF_PARAM;

	for(int iItem = 0; iItem < iItemCnt; iItem++)
	{
		::SendMessage(_hContTab, TCM_GETITEM, iItem, (LPARAM)&tcItem);
		if (((tTbData*)tcItem.lParam) == data)
		{
			bRet = true;
			break;
		}
	}
	return bRet;
}


/*********************************************************************************
 *    Process function of caption bar
 */
LRESULT DockingCont::runProcCaption(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_LBUTTONDOWN:
		{
			_isMouseDown = TRUE;

			if (isInRect(hwnd, LOWORD(lParam), HIWORD(lParam)) == posClose)
			{
				_isMouseClose	= TRUE;
				_isMouseOver	= TRUE;

				/* start hooking */
				hWndServer		= _hCaption;
				if (GetVersion() & 0x80000000)
				{
					hookMouse	= ::SetWindowsHookEx(WH_MOUSE, (HOOKPROC)hookProcMouse, _hInst, 0);
				}
				else
				{
					hookMouse	= ::SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC)hookProcMouse, _hInst, 0);
				}

				if (!hookMouse)
				{
					DWORD dwError = ::GetLastError();
					TCHAR  str[128];
					::wsprintf(str, "GetLastError() returned %lu", dwError);
					::MessageBox(NULL, str, "SetWindowsHookEx(MOUSE) failed", MB_OK | MB_ICONERROR);
				}
				::RedrawWindow(hwnd, NULL, NULL, TRUE);
			}

			focusClient();
			return TRUE;
		}
		case WM_LBUTTONUP:
		{
			_isMouseDown = FALSE;
			if (_isMouseClose == TRUE)
			{
				/* end hooking */
				::UnhookWindowsHookEx(hookMouse);

				if (_isMouseOver == TRUE)
				{
					doClose();
				}
				_isMouseClose	= FALSE;
				_isMouseOver	= FALSE;
			}
			
			focusClient();
			return TRUE;
		}
		case WM_LBUTTONDBLCLK:
		{
			if (isInRect(hwnd, LOWORD(lParam), HIWORD(lParam)) == posCaption)
				::SendMessage(_hParent, DMM_FLOATALL, 0, (LPARAM)this);

			focusClient();
			return TRUE;
		}
		case WM_MOUSEMOVE:
		{
			POINT	pt			= {0};

			/* get correct cursor position */
			::GetCursorPos(&pt);
			::ScreenToClient(_hCaption, &pt);

			if (_isMouseDown == TRUE)
			{
				if (_isMouseClose == FALSE)
				{
                    /* keep sure that button is still down and within caption */
                    if ((wParam == MK_LBUTTON) && (isInRect(hwnd, pt.x, pt.y) == posCaption))
                    {
    					_dragFromTab = FALSE;
    					NotifyParent(DMM_MOVE);
    					_isMouseDown = FALSE;
                    }
                    else
                    {
                        _isMouseDown = FALSE;
                    }
				}
				else
				{
					BOOL    isMouseOver	= _isMouseOver;
					_isMouseOver = (isInRect(hwnd, pt.x, pt.y) == posClose ? TRUE : FALSE);

					/* if state is changed draw new */
					if (_isMouseOver != isMouseOver)
					{
						::SetFocus(NULL);
						::RedrawWindow(hwnd, NULL, NULL, TRUE);
					}
				}
			}
			return TRUE;
		}
		case WM_SIZE:
		{
			::GetWindowRect(hwnd, &_rcCaption);
			ScreenToClient(hwnd, &_rcCaption);
			break;
		}
		case WM_SETTEXT:
		{
			::RedrawWindow(hwnd, NULL, NULL, TRUE);
			return TRUE;
		}
		default:
			break;
	}

	return ::CallWindowProc(_hDefaultCaptionProc, hwnd, Message, wParam, lParam);
}

void DockingCont::drawCaptionItem(DRAWITEMSTRUCT *pDrawItemStruct)
{
	RECT		rc		= pDrawItemStruct->rcItem;
	HDC			hDc		= pDrawItemStruct->hDC;
	HBRUSH		bgbrush	= ::CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
	HPEN		hPen	= ::CreatePen(PS_SOLID, 1, RGB(0xC0,0xC0,0xC0));
	BITMAP		bmp		= {0};
	HBITMAP		hBmpCur	= NULL;
	HBITMAP		hBmpOld = NULL;
	HBITMAP		hBmpNew	= NULL;
	UINT		length  = strlen(_pszCaption);

	int nSavedDC = ::SaveDC(hDc);

	/* begin with paint */
	::SetBkMode(hDc, TRANSPARENT);

	/* set text and/or caption grid */
	if (_isTopCaption == TRUE)
	{
		SIZE	size;

		/* fill background */
		rc.left		+= 1;
		rc.top		+= 2;
		rc.right	-= 16;
		::FillRect(hDc, &rc, bgbrush);

		/* draw text */
		::SelectObject(hDc, _hFont);
		::ExtTextOut(hDc, rc.left, 2, ETO_CLIPPED, &rc, _pszCaption, length, NULL);

		/* calculate beginning of grid lines */
		GetTextExtentPoint32(hDc, _pszCaption, length, &size);
		rc.left += (size.cx + 2);

		if (rc.left < rc.right)
		{
			/* draw grid lines */
			HPEN	hOldPen = (HPEN)::SelectObject(hDc, hPen);

			MoveToEx(hDc, rc.left , rc.top+1 , NULL);
			LineTo  (hDc, rc.right, rc.top+1 );
			MoveToEx(hDc, rc.left , rc.top+4 , NULL);
			LineTo  (hDc, rc.right, rc.top+4 );
			MoveToEx(hDc, rc.left , rc.top+7 , NULL);
			LineTo  (hDc, rc.right, rc.top+7 );
			MoveToEx(hDc, rc.left , rc.top+10, NULL);
			LineTo  (hDc, rc.right, rc.top+10);
		}
	}
	else
	{
		/* create local font for vertical draw */
		HFONT	hFont;
		SIZE	size;

		/* fill background */
		rc.left		+= 2;
		rc.top		+= 16;
		rc.bottom	-= 1;
		::FillRect(hDc, &rc, bgbrush);

		/* draw text */
		hFont = ::CreateFont(12, 0, 90 * 10, 0,
			 FW_NORMAL, FALSE, FALSE, FALSE,
			 ANSI_CHARSET, OUT_DEFAULT_PRECIS,
			 CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
			 DEFAULT_PITCH | FF_ROMAN,
			 "MS Shell Dlg");
		::SelectObject(hDc, hFont);
		::ExtTextOut(hDc, 3, rc.bottom, ETO_CLIPPED, &rc, _pszCaption, length, NULL);

		/* calculate beginning of grid lines */
		GetTextExtentPoint32(hDc, _pszCaption, length, &size);
		rc.bottom -= (size.cx + 2);

		::DeleteObject(hFont);

		if (rc.bottom < rc.top)
		{
			/* draw grid lines */
			HPEN	hOldPen = (HPEN)::SelectObject(hDc, hPen);
			MoveToEx(hDc, rc.left+1 , rc.top, NULL);
			LineTo  (hDc, rc.left+1 , rc.bottom);
			MoveToEx(hDc, rc.left+4 , rc.top, NULL);
			LineTo  (hDc, rc.left+4 , rc.bottom);
			MoveToEx(hDc, rc.left+7 , rc.top, NULL);
			LineTo  (hDc, rc.left+7 , rc.bottom);
			MoveToEx(hDc, rc.left+10, rc.top, NULL);
			LineTo  (hDc, rc.left+10, rc.bottom);
		}
	}
	::DeleteObject(hPen);
	::DeleteObject(bgbrush);

	/* draw button */
	HDC			dcMem		= ::CreateCompatibleDC(NULL);

	/* select correct bitmap */
	if ((_isMouseOver == TRUE) && (_isMouseDown == TRUE))
		hBmpCur = ::LoadBitmap(_hInst, MAKEINTRESOURCE(IDB_CLOSE_DOWN));
	else
		hBmpCur = ::LoadBitmap(_hInst, MAKEINTRESOURCE(IDB_CLOSE_UP));

	/* blit bitmap into the destination */
	::GetObject(hBmpCur, sizeof(bmp), &bmp);
	hBmpOld = (HBITMAP)::SelectObject(dcMem, hBmpCur);
	hBmpNew = ::CreateCompatibleBitmap(dcMem, bmp.bmWidth, bmp.bmHeight);

	rc = pDrawItemStruct->rcItem;
	::SelectObject(hDc, hBmpNew);

	if (_isTopCaption == TRUE)
		::BitBlt(hDc, rc.right-bmp.bmWidth-2, 2, bmp.bmWidth, bmp.bmHeight, dcMem, 0, 0, SRCCOPY);
	else
		::BitBlt(hDc, 2, 2, bmp.bmWidth, bmp.bmHeight, dcMem, 0, 0, SRCCOPY);

	::SelectObject(dcMem, hBmpOld);
	::DeleteObject(hBmpCur);
	::DeleteObject(hBmpNew);
	::DeleteDC(dcMem);

	::RestoreDC(hDc, nSavedDC);
}

eMousePos DockingCont::isInRect(HWND hwnd, int x, int y)
{
	RECT		rc;
	eMousePos	ret	= posOutside;

	::GetWindowRect(hwnd, &rc);
	ScreenToClient(hwnd, &rc);

	if (_isTopCaption == TRUE)
	{
		if ((x > rc.left) && (x < rc.right-16) && (y > rc.top) && (y < rc.bottom))
		{
			ret = posCaption;
		}
		else if ((x > rc.right-14) && (x < rc.right-2) && (y > rc.top+2) && (y < rc.bottom-2))
		{
			ret = posClose;
		}
	}
	else
	{
		if ((x > rc.left) && (x < rc.right) && (y > rc.top+16) && (y < rc.bottom))
		{
			ret = posCaption;
		}
		else if ((x > rc.left-2) && (x < rc.right-2) && (y > rc.top+2) && (y < rc.top+14))
		{
			ret = posClose;
		}
	}

	return ret;
}


/*********************************************************************************
 *    Process function of tab
 */
LRESULT DockingCont::runProcTab(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_LBUTTONDOWN:
		{
			_beginDrag	= TRUE;
			return TRUE;
		}
		case WM_LBUTTONUP:
		{
			int				iItem	= 0;
			TCHITTESTINFO	info	= {0};

			/* get selected sub item */
			info.pt.x = LOWORD(lParam);
			info.pt.y = HIWORD(lParam);
			iItem = ::SendMessage(hwnd, TCM_HITTEST, 0, (LPARAM)&info);

			SelectTab(iItem);
			_beginDrag = FALSE;
			return TRUE;
		}
		case WM_LBUTTONDBLCLK:
		{
			NotifyParent((_isFloating == true)?DMM_DOCK:DMM_FLOAT);
			return TRUE;
		}
		case WM_MBUTTONUP:
		{
			int				iItem	= 0;
			TCITEM			tcItem	= {0};
			TCHITTESTINFO	info	= {0};

			/* get selected sub item */
			info.pt.x = LOWORD(lParam);
			info.pt.y = HIWORD(lParam);
			iItem = ::SendMessage(hwnd, TCM_HITTEST, 0, (LPARAM)&info);

			SelectTab(iItem);

			/* get data and hide toolbar */
			tcItem.mask		= TCIF_PARAM;
			::SendMessage(hwnd, TCM_GETITEM, iItem, (LPARAM)&tcItem);

			/* notify child windows */
			if (NotifyParent(DMM_CLOSE) == 0)
			{
				hideToolbar((tTbData*)tcItem.lParam);
			}
			return TRUE;
		}
		case WM_MOUSEMOVE:
		{
			if ((_beginDrag == TRUE) && (wParam == MK_LBUTTON))
			{
				int				iItem	= 0;
				TCHITTESTINFO	info	= {0};

				/* get selected sub item */
				info.pt.x = LOWORD(lParam);
				info.pt.y = HIWORD(lParam);
				iItem = ::SendMessage(hwnd, TCM_HITTEST, 0, (LPARAM)&info);

				SelectTab(iItem);

				/* send moving message to parent window */
				_dragFromTab = TRUE;
				NotifyParent(DMM_MOVE);
				_beginDrag = FALSE;
			}
            else
            {
                _beginDrag = FALSE;
            }
			return TRUE;
		}
		case WM_NOTIFY:
		{
			LPNMHDR	lpnmhdr = (LPNMHDR)lParam;

			if ((lpnmhdr->hwndFrom == _hContTab) && (lpnmhdr->code == TCN_GETOBJECT))
			{
				int				iItem	= 0;
				TCHITTESTINFO	info	= {0};

				/* get selected sub item */
				info.pt.x = LOWORD(lParam);
				info.pt.y = HIWORD(lParam);
				iItem = ::SendMessage(hwnd, TCM_HITTEST, 0, (LPARAM)&info);

				SelectTab(iItem);
			}
			break;
		}
		default:
			break;
	}

	return ::CallWindowProc(_hDefaultTabProc, hwnd, Message, wParam, lParam);
}

void DockingCont::drawTabItem(DRAWITEMSTRUCT *pDrawItemStruct)
{
	TCITEM	tcItem		= {0};
	RECT	rc			= pDrawItemStruct->rcItem;
	
	int		nTab		= pDrawItemStruct->itemID;
	bool	isSelected	= (nTab == getActiveTb());

	/* get current selected item */
	tcItem.mask = TCIF_PARAM;
	::SendMessage(_hContTab, TCM_GETITEM, nTab, (LPARAM)&tcItem);

	char*	text	= ((tTbData*)tcItem.lParam)->pszName;
	int		length	= strlen(((tTbData*)tcItem.lParam)->pszName);


	/* get drawing context */
	HDC hDc = pDrawItemStruct->hDC;

	int nSavedDC = ::SaveDC(hDc);

	// For some bizarre reason the rcItem you get extends above the actual
	// drawing area. We have to workaround this "feature".
	rc.top += ::GetSystemMetrics(SM_CYEDGE);

	::SetBkMode(hDc, TRANSPARENT);
	HBRUSH hBrush = ::CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
	::FillRect(hDc, &rc, hBrush);
	::DeleteObject((HGDIOBJ)hBrush);

	/* draw orange bar */
	if (isSelected == true)
	{
		RECT barRect  = rc;
		barRect.top  += rc.bottom - 4;

		hBrush = ::CreateSolidBrush(RGB(250, 170, 60));
		::FillRect(hDc, &barRect, hBrush);
		::DeleteObject((HGDIOBJ)hBrush);

	}

	/* draw icon if enabled */
	if (((tTbData*)tcItem.lParam)->uMask & DWS_ICONTAB)
	{
		HIMAGELIST	hImageList	= (HIMAGELIST)::SendMessage(_hParent, DMM_GETIMAGELIST, 0, 0);
		int			iPosImage	= ::SendMessage(_hParent, DMM_GETICONPOS, 0, (LPARAM)((tTbData*)tcItem.lParam)->hClient);

		if ((hImageList != NULL) && (iPosImage >= 0))
		{
			/* Get height of image so we */
			SIZE		size		= {0};
			IMAGEINFO	info		= {0};
			RECT &		imageRect	= info.rcImage;
			
			ImageList_GetImageInfo(hImageList, iPosImage, &info);

			/* calculate position of rect */
			::GetTextExtentPoint(hDc, text, length, &size);
			rc.left += ((rc.right - rc.left) - (imageRect.right - imageRect.left) - size.cx - 2) / 2;

			ImageList_Draw(hImageList, iPosImage, hDc, rc.left, ((isSelected == true)?2:3), ILD_NORMAL);

			rc.left += imageRect.right - imageRect.left - ((isSelected == true)?3:0);
		}
	}

	COLORREF _unselectedColor = RGB(0, 0, 0);
	::SetTextColor(hDc, _unselectedColor);

	/* draw text */
	rc.top -= ::GetSystemMetrics(SM_CYEDGE);
	::SelectObject(hDc, _hFont);
	::DrawText(hDc, text, length, &rc, DT_SINGLELINE|DT_VCENTER|DT_CENTER);

	::RestoreDC(hDc, nSavedDC);
}


/*********************************************************************************
 *    Process function of dialog
 */
BOOL CALLBACK DockingCont::run_dlgProc(UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message) 
	{
		case WM_NCACTIVATE:
		{
			/* Note: lParam to identify the trigger window */
			if ((int)lParam != -1)
			{
				::SendMessage(_hParent, WM_NCACTIVATE, wParam, 0);
			}
			break;
		}
		case WM_INITDIALOG:
		{
			_hContTab = ::GetDlgItem(_hSelf, IDC_TAB_CONT);
			_hCaption = ::GetDlgItem(_hSelf, IDC_BTN_CAPTION);

			/* intial subclassing of caption */
			::SetWindowLong(_hCaption, GWL_USERDATA, reinterpret_cast<LONG>(this));
			_hDefaultCaptionProc = reinterpret_cast<WNDPROC>(::SetWindowLong(_hCaption, GWL_WNDPROC, reinterpret_cast<LONG>(wndCaptionProc)));

			/* intial subclassing of tab */
			::SetWindowLong(_hContTab, GWL_USERDATA, reinterpret_cast<LONG>(this));
			_hDefaultTabProc = reinterpret_cast<WNDPROC>(::SetWindowLong(_hContTab, GWL_WNDPROC, reinterpret_cast<LONG>(wndTabProc)));
			break;
		}
		case WM_NCCALCSIZE:
		case WM_SIZE:
		{
			onSize();
			break;
		}
		case WM_DRAWITEM :
		{
			/* draw tab or caption */
			if (((DRAWITEMSTRUCT *)lParam)->CtlID == IDC_TAB_CONT)
			{
				drawTabItem((DRAWITEMSTRUCT *)lParam);
				return TRUE;
			}
			else
			{
				drawCaptionItem((DRAWITEMSTRUCT *)lParam);
				return TRUE;
			}
			break;
		}
		case WM_NCLBUTTONDBLCLK :
		{
			RECT	rcWnd		= {0};
			RECT	rcClient	= {0};
			POINT	pt			= {HIWORD(lParam), LOWORD(lParam)};

			getWindowRect(rcWnd);
			getClientRect(rcClient);
			ClientToScreen(_hSelf, &rcClient);
			rcWnd.bottom = rcClient.top;

			/* if in caption */
			if ((rcWnd.top  < pt.x) && (rcWnd.bottom > pt.x) &&
				(rcWnd.left < pt.y) && (rcWnd.right  > pt.y))
			{
				NotifyParent(DMM_DOCKALL);
				return TRUE;
			}
			break;
		}
		case WM_SYSCOMMAND :
		{
			switch (wParam & 0xfff0)
			{
				case SC_MOVE:
					NotifyParent(DMM_MOVE);
					return TRUE;
				default: 
					break;
			}
			return FALSE;
		}
		case WM_COMMAND : 
		{
			switch (LOWORD(wParam))
			{   
				case IDCANCEL:
					doClose();
					return TRUE;
				default :
					break;
			}
			break;
		}
		default:
			break;
	}

	return FALSE;
}

void DockingCont::onSize(void)
{
	TCITEM	tcItem		= {0};
	RECT	rc			= {0};
	RECT	rcTemp		= {0};
	UINT	iItemCnt	= ::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0);

	getClientRect(rc);

	if (iItemCnt >= 1)
	{
		/* resize to docked window */
		if (_isFloating == false)
		{
			/* draw caption */
			if (_isTopCaption == TRUE)
			{
				::SetWindowPos(_hCaption, NULL, rc.left, rc.top, rc.right, 16, SWP_NOZORDER | SWP_NOACTIVATE);
				rc.top		+= 16;
				rc.bottom	-= 16;
			}
			else
			{
				::SetWindowPos(_hCaption, NULL, rc.left, rc.top, 16, rc.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
				rc.left		+= 16;
				rc.right	-= 16;
			}

			if (iItemCnt >= 2)
			{
				/* resize tab and plugin control if tabs exceeds one */
				/* resize tab */
				rcTemp = rc;
				rcTemp.top    = (rcTemp.bottom + rcTemp.top) - 22;
				rcTemp.bottom = 20;

				::SetWindowPos(_hContTab, NULL,
								rcTemp.left, rcTemp.top, rcTemp.right, rcTemp.bottom, 
								SWP_NOZORDER | SWP_SHOWWINDOW |  SWP_NOACTIVATE);

				/* resize client area for plugin */
				rcTemp = rc;
				rcTemp.top    += 2;
				rcTemp.bottom -= 22;
			}
			else
			{
				/* resize client area for plugin */
				rcTemp = rc;
				rcTemp.top    += 2;
				rcTemp.bottom -= 2;
			}

			/* set position of client area */
			::SetWindowPos(::GetDlgItem(_hSelf, IDC_CLIENT_TAB), NULL,
							rcTemp.left, rcTemp.top, rcTemp.right, rcTemp.bottom, 
							SWP_NOZORDER | SWP_NOACTIVATE);
		}
		/* resize to float window */
		else
		{
			/* update floating size */
			if (_isFloating == true)
			{
				for (size_t iTb = 0; iTb < _vTbData.size(); iTb++)
				{
					getWindowRect(_vTbData[iTb]->rcFloat);
				}
			}			

			/* draw caption */
			if (iItemCnt >= 2)
			{
				/* resize tab if size of elements exceeds one */
				rcTemp = rc;
				rcTemp.top    = rcTemp.bottom - 22;
				rcTemp.bottom = 20;

				::SetWindowPos(_hContTab, NULL,
								rcTemp.left, rcTemp.top, rcTemp.right, rcTemp.bottom, 
								SWP_NOZORDER | SWP_SHOWWINDOW);
			}

			/* resize client area for plugin */
			rcTemp = rc;
			rcTemp.bottom -= ((iItemCnt == 1)?0:20);

			::SetWindowPos(::GetDlgItem(_hSelf, IDC_CLIENT_TAB), NULL,
							rcTemp.left, rcTemp.top, rcTemp.right, rcTemp.bottom, 
							SWP_NOZORDER | SWP_NOACTIVATE);
		}
		

		/* get active item data */
		UINT	iItemCnt = ::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0);

		/* resize visible plugin windows */
		for (UINT iItem = 0; iItem < iItemCnt; iItem++)
		{
			tcItem.mask		= TCIF_PARAM;
			::SendMessage(_hContTab, TCM_GETITEM, iItem, (LPARAM)&tcItem);

			::SetWindowPos(((tTbData*)tcItem.lParam)->hClient, NULL,
							0, 0, rcTemp.right, rcTemp.bottom, 
							SWP_NOZORDER);
		}
	}
}

void DockingCont::doClose(void)
{
	INT	iItemOff	= 0;
	INT	iItemCnt	= ::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0);

	for (INT iItem = 0; iItem < iItemCnt; iItem++)
	{
		TCITEM		tcItem		= {0};

		/* get item data */
		SelectTab(iItemOff);
		tcItem.mask	= TCIF_PARAM;
		::SendMessage(_hContTab, TCM_GETITEM, iItemOff, (LPARAM)&tcItem);

		/* notify child windows */
		if (NotifyParent(DMM_CLOSE) == 0)
		{
			/* delete tab */
			hideToolbar((tTbData*)tcItem.lParam);
		}
		else
		{
			iItemOff++;
		}
	}

	if (iItemOff == 0)
	{
		/* hide dialog first */
		this->doDialog(false);
		::SendMessage(_hParent, WM_SIZE, 0, 0);
	}
}

void DockingCont::showToolbar(tTbData* pTbData, BOOL state)
{
	if (state == SW_SHOW)
	{
		viewToolbar(pTbData);
	}
	else
	{
		hideToolbar(pTbData);
	}
}

int DockingCont::hideToolbar(tTbData *pTbData)
{
	int		iItem	= SearchPosInTab(pTbData);

	/* delete item */
	if (TRUE == ::SendMessage(_hContTab, TCM_DELETEITEM, iItem, 0))
	{
		UINT	iItemCnt = ::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0);

		if (iItemCnt != 0)
		{
			TCITEM		tcItem = {0};

			tcItem.mask	= TCIF_PARAM;

			if (iItem == iItemCnt)
			{
				iItem--;
			}

			/* activate new selected item and view plugin dialog */
			_prevItem = iItem;
			SelectTab(iItem);

			/* hide tabs if only one element */
			if (iItemCnt == 1)
			{
				::ShowWindow(_hContTab, SW_HIDE);
			}
		}
		else 
		{
			/* hide dialog */
			this->doDialog(false);

			/* send message to docking manager for resize */
			if (_isFloating == false)
			{
				::SendMessage(_hParent, WM_SIZE, 0, 0);
			}
		}

		/* keep sure, that client is hide!!! */
		::ShowWindow(pTbData->hClient, SW_HIDE);
	}
	onSize();

	return iItem;
}

void DockingCont::viewToolbar(tTbData *pTbData)
{
	TCITEM		tcItem		= {0};
	int			iItemCnt	= ::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0);

	if (iItemCnt > 0)
	{
		UINT	iItem	= getActiveTb();

		tcItem.mask		= TCIF_PARAM;
		::SendMessage(_hContTab, TCM_GETITEM, iItem, (LPARAM)&tcItem);
		
		/* hide active dialog */
		::ShowWindow(((tTbData*)tcItem.lParam)->hClient, SW_HIDE);
	}

	/* create new tab if it not exist */
	int iTabPos = SearchPosInTab(pTbData);
	if (iTabPos == -1)
	{
		/* set only params and text even if icon available */
		tcItem.mask			= TCIF_PARAM | TCIF_TEXT;

		tcItem.lParam		= (LPARAM)pTbData;

		if (pTbData->uMask & DWS_ICONTAB)
		{
			/* fake here a icon before the text ... */
			char	szText[64];

			strcpy(szText, "    ");
			strcat(szText, pTbData->pszName);

			tcItem.pszText		= szText;
			tcItem.cchTextMax	= strlen(szText);
		}
		else
		{
			/* ... but here put text normal into the tab */
			tcItem.pszText		= pTbData->pszName;
			tcItem.cchTextMax	= strlen(pTbData->pszName);
		}

		::SendMessage(_hContTab, TCM_INSERTITEM, iItemCnt, (LPARAM)&tcItem);
		SelectTab(iItemCnt);
	}
	/* if exists select it and update data */
	else
	{
		tcItem.mask			= TCIF_PARAM;
		tcItem.lParam		= (LPARAM)pTbData;
		::SendMessage(_hContTab, TCM_SETITEM, iTabPos, (LPARAM)&tcItem);
		SelectTab(iTabPos);
	}

	/* show dialog and notify parent to update dialog view */
	if (isVisible() == false)
	{
		this->doDialog();
		::SendMessage(_hParent, WM_SIZE, 0, 0);
	}

	/* set position of client */
	onSize();
}

int DockingCont::SearchPosInTab(tTbData* pTbData)
{
	TCITEM	tcItem		= {0};
	int		iItemCnt	= ::SendMessage(_hContTab, TCM_GETITEMCOUNT, 0, 0);
	int		ret			= -1;

	tcItem.mask	= TCIF_PARAM;

	for (int iItem = 0; iItem < iItemCnt; iItem++)
	{
		::SendMessage(_hContTab, TCM_GETITEM, iItem, (LPARAM)&tcItem);

		if (((tTbData*)tcItem.lParam)->hClient == pTbData->hClient)
		{
			ret = iItem;
			break;
		}
	}
	return ret;
}

void DockingCont::SelectTab(int iItem)
{
	if (iItem != -1)
	{
		TCITEM			tcItem	= {0};

		/* get data of new active dialog */
		tcItem.mask		= TCIF_PARAM;
		::SendMessage(_hContTab, TCM_GETITEM, iItem, (LPARAM)&tcItem);

		/* show active dialog */
		::ShowWindow(((tTbData*)tcItem.lParam)->hClient, SW_SHOW);
		::SetFocus(((tTbData*)tcItem.lParam)->hClient);

		if (iItem != _prevItem)
		{
			/* hide previous dialog */
			::SendMessage(_hContTab, TCM_GETITEM, _prevItem, (LPARAM)&tcItem);
			::ShowWindow(((tTbData*)tcItem.lParam)->hClient, SW_HIDE);
		}

		/* selects the pressed tab and store previous tab */
		::SendMessage(_hContTab, TCM_SETCURSEL, iItem, 0);
		_prevItem = iItem;

		/* update caption text */
		updateCaption();

		onSize();
	}
}

void DockingCont::updateCaption(void)
{
	TCITEM			tcItem	= {0};
	int				iItem	= 0;

	/* get active tab */
	iItem = getActiveTb();

	/* get data of new active dialog */
	tcItem.mask		= TCIF_PARAM;
	::SendMessage(_hContTab, TCM_GETITEM, iItem, (LPARAM)&tcItem);

	/* update caption text */
	strcpy(_pszCaption, ((tTbData*)tcItem.lParam)->pszName);

	/* test if additional information are available */
	if ((((tTbData*)tcItem.lParam)->uMask & DWS_ADDINFO) && 
		(strlen(((tTbData*)tcItem.lParam)->pszAddInfo) != 0))
	{
		strcat(_pszCaption, " - ");
		strcat(_pszCaption, ((tTbData*)tcItem.lParam)->pszAddInfo);
	}

	if (_isFloating == true)
	{
		::SetWindowText(_hSelf, _pszCaption);
	}
	else
	{
		::SetWindowText(_hCaption, _pszCaption);
	}
}

void DockingCont::focusClient(void)
{
	TCITEM		tcItem	= {0};
	int			iItem	= getActiveTb();	

	if (iItem != -1)
	{
		/* get data of new active dialog */
		tcItem.mask		= TCIF_PARAM;
		::SendMessage(_hContTab, TCM_GETITEM, iItem, (LPARAM)&tcItem);

		/* set focus */
		::SetFocus(((tTbData*)tcItem.lParam)->hClient);
	}
}

LPARAM DockingCont::NotifyParent(UINT message)
{
	return ::SendMessage(_hParent, message, 0, (LPARAM)this);
}

