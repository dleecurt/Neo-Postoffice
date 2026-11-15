#include "multiple.h"
#include <FL/Fl_Menu_.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/fl_ask.H>
#include <stdlib.h>

#define HOTKEYS "01234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ"

// _find() is from Carl's Styles CVS snapshot of 050499
static Fl_Menu_Item* _find(const char *label, Fl_Menu_Item *menu) {
  char *p, l[128];
  Fl_Menu_Item *m;

  strncpy(l, label, sizeof(l));
  l[sizeof(l) - 1] = (char)0;

  if ((p = strchr(l, '/'))) *p++ = (char)0;

  for (m = menu; m && m->label() && strcmp(m->label(), l); m = m->next()) ;
  if (!m || !m->label()) return 0;
  if (p) {
    if (m->flags&FL_SUBMENU_POINTER)
      return _find(p, (Fl_Menu_Item *)m->user_data());
    else
      return _find(p, ++m);
  }
  return m;
}

// findsessionmenu is an adaptation of _find() from Carl's Styles
// CVS snapshot of 050499
static Fl_Menu_Item* findsessionmenu(const char *label, Fl_Menu_Item *menu) {
  char *p, l[128];
  Fl_Menu_Item *m;

  strncpy(l, label, sizeof(l));
  l[sizeof(l) - 1] = (char)0;

  if ((p = strchr(l, '/'))) *p++ = (char)0;

  for (m = menu; m && m->label(); m++)
  {
	  const char *temp = m->label() + 3;
	  if(strcmp(temp, l) == 0)
		  break;
  }
  if (!m || !m->label()) return 0;
  if (p) {
    if (m->flags&FL_SUBMENU_POINTER)
      return findsessionmenu(p, (Fl_Menu_Item *)m->user_data());
    else
      return findsessionmenu(p, ++m);
  }
  return m;
}


// **************************************************
//						Callbacks
// **************************************************


static void cb_WindowMenu(Fl_Menu_ *o, Fl_MultiEditor *v) 
{
	const Fl_Menu_Item *chosen = o->mvalue();
	fledit_SessionInfo *inf = v->FindSession(&chosen->text[3]);
	if(inf)
	{
		v->Session(inf);
		myWindow->label(inf->SessionName->Get());
	}
}

Fl_Menu_Item *FindWindowMenu(const Fl_Menu_Item *base)
{
	return(_find("&Window", (Fl_Menu_Item *)base));
}

void AddToWindowMenu(Fl_MultiEditor *editor, long sessid, const char *menuname, Fl_Menu *menu)
{
	fledit_SessionInfo *info = editor->FindSession(sessid);
	if(info)
	{
		wString newmenuname(menuname);
		if(editor->GetSessionCount() < 36)
		{
			char buffer[2];
			buffer[1] = '\0';
			buffer[0] =  HOTKEYS[editor->GetSessionCount()];
			newmenuname += "/&";
			newmenuname += buffer;
			newmenuname += " ";
			newmenuname += info->SessionName->Get();
		}
		else
		{			// next line needs spaces so session name starts in right place
			newmenuname += "/   ";
			newmenuname += info->SessionName->Get();
		}
		*info->Caption = newmenuname;
		menu->add(info->Caption->Get(),0,(Fl_Callback *)cb_WindowMenu, (void *)editor, 0);
	}
}

void RemoveFromWindowMenu(Fl_MultiEditor *editor, long sessid, Fl_Menu_* mainmenu, Fl_Menu* winmenu)
{
	int index = 0;
	fledit_SessionInfo *info = editor->FindSession(sessid);
	if(info)
	{
		const char *hotkey = NULL;
		Fl_Menu_Item *item = findsessionmenu(info->SessionName->Get(), winmenu);
		if(item)
		{
			int oldindex = mainmenu->value(item);	// set chosen menu to this one
			index = mainmenu->value();				// and read its index
			mainmenu->value(oldindex);				// and set its index back
			// value() doesn't seem to work?
			if(index < 2)
				for(; (index < mainmenu->size()) && (&(mainmenu->menu()[index]) != item); index++);
			char hk = item->label() ? item->label()[1] : '\0';
			if(hk)
				hotkey = strchr(HOTKEYS, hk);
			item = item->next();
		}
		while(item && item->label())
		{
			char *lbl = (char *)item->label();
			if((lbl[0] == '&') && (lbl[2] == ' '))
			{
				lbl[1] = *(hotkey);
				hotkey++;
			}
			item = item->next();
		}
	}
	// have to remove this *last* since it may change our menu pointers
	if(index)
		mainmenu->remove(index);
}


void ChangeWindowMenu(Fl_MultiEditor *editor, const char *oldname, Fl_Menu_* mainmenu, Fl_Menu* winmenu)
{
	fledit_SessionInfo *info = editor->FindSession(editor->Session());
	if(info)
	{
		Fl_Menu_Item *item = findsessionmenu(oldname, winmenu);
		if(item)
		{
			wString newname(item->label());
			newname.ChopAt(3);
			newname += info->SessionName->Get();
			*info->Caption = newname;
			item->label(info->Caption->Get());
		}
	}
}

bool CloseSession(Fl_Menu_ *menu, Fl_Menu *WindowMenu, Fl_MultiEditor *ed)
{
	bool OKtoclose = false;
	if(ed->changed())
	{
		int choice = fl_choice("\"%s\" has been modified.  Do you want to save it?",
 								"&Cancel", "&Yes", "&No", ed->SessionName());
		if(choice == 1)
		{
			const char *fn = ed->CurrentFileName();
			if (!fn || (!fn[0]))
			   fn = fl_file_chooser("Save As", NULL, NULL);
			if(fn)
			{
				ed->SaveCurrent(fn);
				OKtoclose = true;
			}
		}
		else if(choice == 2)
			OKtoclose = true;
	}
	else 
		OKtoclose = true;
	if(OKtoclose)
	{
		RemoveFromWindowMenu(ed, ed->Session(), menu, WindowMenu);
		ed->CloseCurrent();
		myWindow->label(ed->SessionName());
	}
	return(OKtoclose);
}

bool ExitOK(Fl_Menu_ *menu, Fl_Menu *WindowMenu, Fl_MultiEditor *ed)
{
	bool OKtoExit = true;
	long sc = ed->ActiveSessions();
	while(sc-- && OKtoExit)
	{
		if(!CloseSession(menu, WindowMenu, ed))
			OKtoExit = false;
	}
	return(OKtoExit);
}


