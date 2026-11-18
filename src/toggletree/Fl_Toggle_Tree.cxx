#include <stdlib.h>
#include <stdio.h>
#include <FL/Fl_Input.H>
#include <FL/Fl_Group.H>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Scroll.H>
#include <vector>

#include <FL/Fl_Toggle_Node.H>
#include <FL/Fl_Toggle_Tree.H>
#include <FL/open_icon.xpm>
#include <FL/closed_icon.xpm>

void Fl_Toggle_Tree::select_range(Fl_Toggle_Node* start,
                                 Fl_Toggle_Node* end, int add) {

  Fl_Toggle_Node *tnode = (Fl_Toggle_Node *) first_;
  int selecting = 0;
  selection_current_ = 0;
  selection_count_ = 0;

  traverse_start(tnode);

  while (tnode) {
    if (tnode == start || tnode == end) {
      if (tnode->selected_ == 0) {
        tnode->selected_ = 1;
        tnode->changed_ = 1;
      }
      if (start != end) selecting = !selecting;
    } else {
      if (selecting) {
        if (tnode->selected_ == 0) {
          tnode->selected_ = 1;
          tnode->changed_ = 1;
        }
      } else {
        int tmp = tnode->selected_;
        tnode->selected_ &= add;
        tnode->changed_ =
          tmp != tnode->selected_;
      }
    }
    tnode = traverse_forward();
  }
}

Fl_Pixmap* Fl_Toggle_Tree::s_closed_pixmap_ = 0;
Fl_Pixmap* Fl_Toggle_Tree::s_opened_pixmap_ = 0;

static const int no_columns[1] = {0};

Fl_Toggle_Tree::Fl_Toggle_Tree(int x, int y, int w, int h) : Fl_Toggle_Tree_Base(x, y, w, h) {
  pixmap_offset_ = 16;
  keynav_ = 0;
  label_offset_ = 40;
  current_ = 0;
  state_ = FL_TOGGLE_NONE;
  drag_enabled_ = 0;
  dragging_ = 0;
  push_x_ = push_y_ = 0;
  drag_node_ = 0;
  context_node_ = 0;
  closed_pixmap_ = 0;
  opened_pixmap_ = 0;
  if (s_closed_pixmap_ == 0) {
    s_closed_pixmap_ = new Fl_Pixmap(closed_icon);
  }
  if (s_opened_pixmap_ == 0) {
    s_opened_pixmap_ = new Fl_Pixmap(open_icon);
  }
  closed_pixmap(s_closed_pixmap_);
  opened_pixmap(s_opened_pixmap_);
  column_widths_ = no_columns;
  column_char_ = '\t';

  textfont_ = FL_HELVETICA;
  textsize_ = 12;
  textcolor_ = FL_BLACK;
  edit_input_ = new Fl_Input(x, y, 0, 0);
  edit_input_->box(FL_FLAT_BOX);
  edit_input_->color(FL_WHITE);
#ifndef FLTK_2
  edit_input_->textcolor(FL_BLACK);
  edit_input_->textfont(textfont_);
  edit_input_->textsize(textsize_);
#endif
  edit_callback((Fl_Callback*) edit_default_callback, this);
  edit_input_->when(FL_WHEN_RELEASE | FL_WHEN_ENTER_KEY | FL_WHEN_NOT_CHANGED);

  edit_input_->hide();
  edit_on_reselect_ = 1;

  color(FL_WHITE);
  selection_color(FL_BLACK);
  selection_label_color(FL_YELLOW);
  alternate_color(FL_LIGHT2);
  trim_color(FL_LIGHT1);
  draw_lines_ = 0;
}

Fl_Toggle_Tree::~Fl_Toggle_Tree() {
  Fl::remove_timeout(key_callback_, this);
  delete edit_input_;
}

int cnt = 0;

void Fl_Toggle_Tree::draw_node(int depth, int cy, Fl_Toggle_Node_Base* node) {
  Fl_Toggle_Node* tnode = (Fl_Toggle_Node*)node;

  if (damage() == FL_DAMAGE_CHILD && !tnode->changed_ && damaged_ == 0) {
    return;
  }

  tnode->changed_ = 0;
  if (tnode->selected_) {
    fl_color(selection_color());
    fl_rectf(x(), cy + 1, w(), height_(tnode) - 1);
  } else {
    fl_color((cy - y()) & 1 ? color() : alternate_color());
    fl_rectf(x(), cy + 1, w(), height_(tnode) - 1);
  }
  fl_color(trim_color());
  fl_line(x(), cy, x() + w(), cy);
  fl_color(FL_BLACK);

  if (draw_lines_)
    {
      int i;
      Fl_Toggle_Node * n;
      fl_xyline(x()+depth*16+8, cy+8, x()+(depth+1)*16, cy+8); 
      if (tnode->next_) 
	fl_xyline(x()+depth*16+8, cy, x()+depth*16+8, cy+16); 
      else 
	fl_xyline(x()+depth*16+8, cy, x()+depth*16+8, cy+8); 
      for (i=depth-1, n = (Fl_Toggle_Node*)tnode->up_; n; i--, 
	     n = (Fl_Toggle_Node*)n->up_) 
	if (n->next_) 
	  fl_xyline(x()+i*16+8, cy, x()+i*16+8, cy+16); 
    }

  if (tnode->can_open_) {
    if (tnode->opened_)
      opened_pixmap_->draw(x() + depth*16, cy);
    else
      closed_pixmap_->draw(x() + depth*16, cy);
  }

#ifndef FLTK_2
  if (tnode->selected_)
    textcolor(selection_label_color());
  else
    textcolor(((Fl_Toggle_Node *)node)->color());
#endif

  if (tnode->label_) {
    int D = depth * 16 + label_offset_;
    draw_label(tnode->label_, D, x(), cy, w(), 16);
  }
  if (tnode->pixmap_) {
    tnode->pixmap_->draw(x() + depth*16 + pixmap_offset_, cy + 1);
  }
}


void Fl_Toggle_Tree::draw_label(char* str, int indent, int x, int y, int w, int h) {
  const int* i = column_widths();

  while (w > 6) {    // do each tab-seperated field
    int w1 = w;      // width for this field
    char* e = 0;     // pointer to end of field or null if none
    if (*i) { // find end of field and temporarily replace with 0
      for (e = str; *e && *e != column_char(); e++);
      if (*e) {
        *e = 0; w1 = *i++;
      } else e = 0;
    }
    int size = textsize();
    Fl_Font font = textfont();
    Fl_Color lcol = textcolor();
    Fl_Align align = (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP);
    // check for all the @-lines recognized by XForms:
    /*    while (*str == format_char() && *++str && *str != format_char()) {
          switch (*str++) {
          case 'l': case 'L': size = 24; break;
          case 'm': case 'M': size = 18; break;
          case 's': size = 11; break;
          case 'b': font = (Fl_Font)(font|FL_BOLD); break;
          case 'i': font = (Fl_Font)(font|FL_ITALIC); break;
          case 'f': case 't': font = FL_COURIER; break;
          case 'c': align = FL_ALIGN_CENTER; break;
          case 'r': align = FL_ALIGN_RIGHT; break;
          case 'B': 
    	fl_color((Fl_Color)strtol(str, &str, 10));
    	fl_rectf(x, y, w1, h);
            break;
          case 'C':
    	lcol = (Fl_Color)strtol(str, &str, 10);
    	break;
          case 'F':
    	font = (Fl_Font)strtol(str, &str, 10);
    	break;
          case 'N':
    	lcol = FL_INACTIVE_COLOR;
    	break;
          case 'S':
    	size = strtol(str, &str, 10);
    	break;
          case '-':
    	fl_color(FL_DARK3);
    	fl_line(x+3, y+h/2, x+w1-3, y+h/2);
    	fl_color(FL_LIGHT3);
    	fl_line(x+3, y+h/2+1, x+w1-3, y+h/2+1);
    	break;
          case 'u':
          case '_':
    	fl_color(lcol);
    	fl_line(x+3, y+h-1, x+w1-3, y+h-1);
    	break;
          case '.':
    	goto BREAK;
          case '@':
    	str--; goto BREAK;
          }
        }
      BREAK:
    */
    fl_font(font, size);
#ifndef FLTK_2
    if (!active_r()) lcol = fl_inactive (lcol);
#endif
    //    if (((FL_BLINE*)v)->flags & SELECTED)
    //      lcol = contrast(lcol, selection_color());
    fl_color(lcol);
    fl_draw(str, x + indent, y + 1, w1 - indent, h + 1, align, NULL, 0); // no @symbols in item text
    if (!e) break;   // no more fields...
    *e = column_char();   // put the seperator back
    x += w1;
    w -= w1;
    str = e + 1;
    indent = 0;
  }
}

void Fl_Toggle_Tree::open(Fl_Toggle_Node* node) {
  if (node->opened_ == 1)
    return;

  int th = Fl_Toggle_Tree_Base::open(node);

  damaged_ = node;
  if (th) {
    damage(FL_DAMAGE_TREE);
  } else {
    damage(FL_DAMAGE_CHILD);
  }
  if (th) {
    resize(x(), y(), w(), h() + th);
    parent()->damage(FL_DAMAGE_CHILD);
  }
}

void Fl_Toggle_Tree::close(Fl_Toggle_Node* node) {
  if (node->opened_ == 0)
    return;

  int th = Fl_Toggle_Tree_Base::close(node);

  damaged_ = node;
  if (th) {
    damage(FL_DAMAGE_TREE);
    resize(x(), y(), w(), h() - th);
    parent()->damage(FL_DAMAGE_SCROLL);
  } else {
    damage(FL_DAMAGE_CHILD);
  }
}

void Fl_Toggle_Tree::edit(Fl_Toggle_Node *t, int cx, int cy) {
  // printf("edit\n");
  if (!edit_input_->visible() && t) {
    // printf("%d %d %d %d %s\n",cx, cy, w()-(cx-x()), height_(t), t->label());
    edit_input_->resize(cx - 3, cy + 1, w() - (cx - 3 - x()), height_(t) - 1);
    edit_input_->value(t->label());
    edit_input_->show();
    edit_input_->take_focus();
  }
}

void Fl_Toggle_Tree::edit_default_callback(Fl_Input*, void* ptr) {
  //  printf("default_edit_cb\n");
  Fl_Toggle_Tree* tree = (Fl_Toggle_Tree*) ptr;
  tree->end_edit();
}

void Fl_Toggle_Tree::end_edit(void) {
  //  printf("end_edit\n");
  if (current_) {
    ((Fl_Toggle_Node *)current_)->label(strdup(edit_input_->value()));
  }
  edit_input_->hide();

  damaged_ = current_;
  damage(FL_DAMAGE_CHILD);
}

int Fl_Toggle_Tree::handle(int event) {
  //  printf("event: %d\n",event);
  static Fl_Toggle_Node* prev = 0;
  switch (event) {
  case FL_ENTER:
    return 1;
  case FL_FOCUS:
  case FL_UNFOCUS:
    return keynav_;
  case FL_KEYBOARD:
    return keynav_ ? handle_key(Fl::event_key()) : 0;
  case FL_PUSH: {
    if (keynav_ && Fl::focus() != this) take_focus();
    dragging_ = 0;
    drag_node_ = 0;
    if (Fl::event_button() == FL_RIGHT_MOUSE) {
      int depth, cy;
      context_node_ = (Fl_Toggle_Node*) Fl_Toggle_Tree_Base::find(Fl::event_y(), depth, cy);
      state_ = FL_TOGGLE_CONTEXT;
      do_callback();
      context_node_ = 0;
      return 1;
    }
    if (drag_enabled_) {
      int depth, cy;
      push_x_ = Fl::event_x();
      push_y_ = Fl::event_y();
      drag_node_ = (Fl_Toggle_Node*) Fl_Toggle_Tree_Base::find(Fl::event_y(), depth, cy);
    }
    return 1;
  }
  case FL_DRAG: {
    if (drag_enabled_ && drag_node_ && !dragging_ &&
        (abs(Fl::event_x() - push_x_) > 5 || abs(Fl::event_y() - push_y_) > 5)) {
      dragging_ = 1;
      if (window()) window()->cursor(FL_CURSOR_MOVE);
    }
    return 1;
  }
  case FL_RELEASE: {
      if (Fl::event_button() == FL_RIGHT_MOUSE) return 1;   // handled on push
      if (dragging_) {
        dragging_ = 0;
        if (window()) window()->cursor(FL_CURSOR_DEFAULT);
        state_ = FL_TOGGLE_DROP;
        do_callback();
        drag_node_ = 0;
        return 1;
      }
      if (edit_input_->visible()) {
        end_edit();
      }
      int depth;
      int cy;
      Fl_Toggle_Node* tnode = (Fl_Toggle_Node*) Fl_Toggle_Tree_Base::find(Fl::event_y(), depth, cy);
      if (!tnode) return 1;   // below the last node
      if (Fl::event_x() < x() + depth*16 + 16) {
        if (tnode->opened_) {
          current_ = tnode;
          state_ = FL_TOGGLE_OPENED;
          do_callback();
          close(tnode);
        } else {
          current_ = tnode;
          state_ = FL_TOGGLE_CLOSED;
          do_callback();
          open(tnode);
        }
      } else {
        if (Fl::event_state(FL_SHIFT)) {
          if (prev == 0) prev = tnode;
          select_range(prev, tnode, 1);
          current_ = 0;
          state_ = FL_TOGGLE_SELECT;
          do_callback();
        } else if (Fl::event_state(FL_CTRL)) {
          if (!tnode->selected_)
            select_range(tnode, tnode, 1);   // add (was the FL_CTRL bit, which "&=" turned into 0)
          else {
	    selection_current_ = NULL;
            tnode->selected_ = 0;
            tnode->changed_ = 1;
            tnode = 0;
          }
          current_ = 0;
          state_ = FL_TOGGLE_SELECT;
          do_callback();
        } else {
          select_range(tnode, tnode, 0);
          state_ = Fl::event_clicks() ? FL_TOGGLE_HIT : FL_TOGGLE_SELECT;
          if (tnode == current_ && state_ == FL_TOGGLE_SELECT) {
            state_ = FL_TOGGLE_RESELECT;
          }
          current_ = tnode;
          if (state_ == FL_TOGGLE_RESELECT && edit_on_reselect_) {
            edit(tnode, x() + depth*16 + label_offset_, cy);
          }

          do_callback();
        }
        damaged_ = 0;
        damage(FL_DAMAGE_CHILD);
        prev = tnode;
      }
    }
    return 1;
  }
  return 0;
}

Fl_Toggle_Node* Fl_Toggle_Tree::selection(void) {

  if (current_ == 0)
    current_ = traverse_start();
  else {
    traverse_start(current_);
    current_ = traverse_forward();
  }

  while (current_) {
    if (((Fl_Toggle_Node *)current_)->selected_) {
      return (Fl_Toggle_Node *)current_;
    }
    current_ = traverse_forward();
  }
  return 0;
}


int Fl_Toggle_Tree::selection_count(void) {
  if (selection_current_ == 0)
    selection_count_ = 0;

  if (selection_count_)
    return selection_count_;

  Fl_Toggle_Node* t;
  t = traverse_start();
  while (t) {
    if (t->selected_) {
      selection_count_++;
    }
    t = traverse_forward();
  }

  return selection_count_;
}


Fl_Toggle_Node* Fl_Toggle_Tree::selection(int i) {
  int backwards = 0;

  if (selection_current_ == 0) {
    selection_current_ = traverse_start();
    selection_i_ = 0;
  } else {
    traverse_start(selection_current_);
    if (i > selection_i_) {
      selection_current_ = traverse_forward();
      selection_i_ ++;
    } else if (i < selection_i_) {
      selection_current_ = traverse_backward();
      selection_i_ --;
      backwards = 1;
    }
  }

  if (backwards) {
    while (selection_current_) {
      //printf("traversed bw to:%s\n",selection_current_->label());
      if (selection_current_->selected_) {
        if (i == selection_i_) {
          return selection_current_;
        }
        selection_i_ --;
      }
      selection_current_ = traverse_backward();
    }
  } else {
    while (selection_current_) {
      if (selection_current_->selected_) {
        if (i == selection_i_) {
          return selection_current_;
        }
        selection_i_ ++;
      }
      selection_current_ = traverse_forward();
    }
  }
  return 0;
}


void Fl_Toggle_Tree::opened_pixmap(Fl_Pixmap *a) {
  if (opened_pixmap_ && opened_pixmap_ != s_opened_pixmap_) {
    delete opened_pixmap_;
  }
  opened_pixmap_ = a;
}


void Fl_Toggle_Tree::closed_pixmap(Fl_Pixmap *a) {
  if (closed_pixmap_ && closed_pixmap_ != s_closed_pixmap_) {
    delete closed_pixmap_;
  }
  closed_pixmap_ = a;
}


int Fl_Toggle_Tree::remove (char * a) {
  Fl_Toggle_Node * curr;

  curr = find(a);
  if (curr)
    return remove(curr);

  return 0;
}


int Fl_Toggle_Tree::remove (void * a) {
  Fl_Toggle_Node * curr;

  curr = find(a);
  if (curr)
    return remove(curr);

  return 0;
}


Fl_Toggle_Node * Fl_Toggle_Tree::find (char * a) {
  Fl_Toggle_Node * curr = traverse_start();

  while (curr) {
    if (curr->label()) {
      if (!strcmp(curr->label(), a)) {
        return curr;
      }
    }
    curr = traverse_forward();
  }

  return 0;
}


Fl_Toggle_Node * Fl_Toggle_Tree::find (void * a) {
  Fl_Toggle_Node * curr = traverse_start();

  while (curr) {
    if (curr->label()) {
      if (curr->data() == a) {
        return curr;
      }
    }
    curr = traverse_forward();
  }

  return 0;
}


int Fl_Toggle_Tree::sort_by_label(Fl_Toggle_Node_Base* a, Fl_Toggle_Node_Base* b) {
  return strcmp(((Fl_Toggle_Node*)a)->label(), ((Fl_Toggle_Node*)b)->label());
}


void Fl_Toggle_Tree::edit_callback(Fl_Callback* c, void* p) {
  edit_input_->callback((Fl_Callback*)c, p);
}


void Fl_Toggle_Tree::edit_callback(Fl_Callback* c) {
  edit_input_->callback((Fl_Callback*)c);
}


void Fl_Toggle_Tree::edit_callback(Fl_Callback0*c) {
  edit_input_->callback(c);
}


void Fl_Toggle_Tree::edit_callback(Fl_Callback1*c, long p) {
  edit_input_->callback(c, p);
}

//-------------------------------------------------------------------------------
// Neo-PostOffice: keyboard navigation
//-------------------------------------------------------------------------------

// The distance of a visible node from the top of the tree, or -1.
int Fl_Toggle_Tree::node_offset(Fl_Toggle_Node_Base* node) {
  int depth = 0, off = 0;
  traverse_start(first_);
  for (Fl_Toggle_Node_Base* n = first_; n; n = traverse_forward(1, depth)) {
    if (n == node) return off;
    off += height(n);
  }
  return -1;
}

// Scrolls the enclosing Fl_Scroll so that node is in view.
void Fl_Toggle_Tree::show_node(Fl_Toggle_Node_Base* node) {
  Fl_Scroll* scroll = dynamic_cast<Fl_Scroll*>(parent());
  int off = node_offset(node);
  if (!scroll || off < 0) return;
  int top = y() + off, bottom = top + height(node);
  int view_top = scroll->y() + Fl::box_dy(scroll->box());
  int view_bottom = scroll->y() + scroll->h() - Fl::box_dy(scroll->box());
  if (scroll->hscrollbar.visible()) view_bottom -= scroll->hscrollbar.h();
  int ypos = scroll->yposition();
  if (top < view_top) ypos -= view_top - top;
  else if (bottom > view_bottom) ypos += bottom - view_bottom;
  else return;
  if (ypos < 0) ypos = 0;
  scroll->scroll_to(scroll->xposition(), ypos);
  // Fl_Scroll copies the old picture when it scrolls and the tree only
  // redraws changed rows: draw it all, or a stale selection stays.
  scroll->redraw();
}

void Fl_Toggle_Tree::key_callback_(void* v) {
  Fl_Toggle_Tree* tree = (Fl_Toggle_Tree*) v;
  // The list may have been rebuilt since the key press.
  if (!tree->current_ || tree->node_offset(tree->current_) < 0) return;
  tree->state_ = FL_TOGGLE_SELECT;
  tree->do_callback();
}

// Selects node alone, shows it, and calls the callback shortly.
void Fl_Toggle_Tree::key_select(Fl_Toggle_Node_Base* node) {
  if (!node) return;
  select_range((Fl_Toggle_Node*) node, (Fl_Toggle_Node*) node, 0);
  current_ = node;
  damaged_ = 0;
  damage(FL_DAMAGE_CHILD);
  show_node(node);
  Fl::remove_timeout(key_callback_, this);
  Fl::add_timeout(0.15, key_callback_, this);
}

int Fl_Toggle_Tree::handle_key(int key) {
  if (Fl::event_state() & (FL_CTRL | FL_ALT | FL_META)) return 0;
  if (key != FL_Up && key != FL_Down && key != FL_Page_Up && key != FL_Page_Down &&
      key != FL_Home && key != FL_End && key != FL_Left && key != FL_Right)
    return 0;
  if (edit_input_->visible()) return 0;

  // The visible nodes, and where the selection is among them.
  std::vector<Fl_Toggle_Node_Base*> rows;
  int depth = 0, at = -1;
  traverse_start(first_);
  for (Fl_Toggle_Node_Base* n = first_; n; n = traverse_forward(1, depth)) {
    if (n == current_) at = (int) rows.size();
    rows.push_back(n);
  }
  if (rows.empty()) return 1;
  int last = (int) rows.size() - 1;
  int page = 1;
  if (parent() && height(rows[0]) > 0) page = parent()->h() / height(rows[0]) - 1;
  if (page < 1) page = 1;

  if (at < 0) {   // nothing selected: start at the top (or the bottom)
    key_select(rows[key == FL_End || key == FL_Up ? last : 0]);
    return 1;
  }
  Fl_Toggle_Node* node = (Fl_Toggle_Node*) rows[at];
  switch (key) {
  case FL_Up:        if (at > 0) key_select(rows[at - 1]); break;
  case FL_Down:      if (at < last) key_select(rows[at + 1]); break;
  case FL_Page_Up:   key_select(rows[at > page ? at - page : 0]); break;
  case FL_Page_Down: key_select(rows[at + page < last ? at + page : last]); break;
  case FL_Home:      key_select(rows[0]); break;
  case FL_End:       key_select(rows[last]); break;
  case FL_Right:
    if (node->sub_ && !node->opened_) {
      open(node);
      update_height();
      if (parent()) parent()->redraw();
    } else if (node->vsub_) key_select(node->vsub_);
    break;
  case FL_Left:
    if (node->opened_ && node->sub_) {
      close(node);
      update_height();
      if (parent()) parent()->redraw();
      show_node(node);
    } else if (node->up_) key_select(node->up_);
    break;
  }
  return 1;
}
