#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_StatusPanel.H>

  Fl_Window* w;

static void ptimer(void *o)
{
	Fl_StatusPanel *pnl = (Fl_StatusPanel *)o;
	Fl_ProgressBox *pbar = pnl->progressbar();
	if(pbar->position() < 100)
	{
		pbar->step(1);
		Fl::add_timeout(0.1, ptimer, (void *)pnl);
	}
	else
		pnl->showprogress(false);
}

int main(int argc, char **argv) {
	Fl_StatusPanel *stp;
  { Fl_Window* o = new Fl_Window(400, 100);
    w = o;
    { Fl_StatusPanel* o = new Fl_StatusPanel(1, 75, 398, 25, "");
		stp = o;
    }
    o->end();
  }
  stp->status("Test");
  stp->updatestatus();
  stp->showprogress(true);
  Fl::add_timeout(0.1, ptimer, (void *)stp);
  w->show(argc, argv);

  return Fl::run();
}
