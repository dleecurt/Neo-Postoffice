#ifndef Fl_ToggleNode_H
#define Fl_ToggleNode_H

#include <FL/Fl_Toggle_Node_Base.H>
#include <FL/Enumerations.H>
#include <string.h>

class Fl_Pixmap;

class Fl_Toggle_Node : public Fl_Toggle_Node_Base {

  friend class Fl_Toggle_Tree;

public:

  Fl_Toggle_Node(char* label = 0, int can_open = 1, Fl_Pixmap* pixmap = 0,
                void * d = 0) : Fl_Toggle_Node_Base() {
    vsub_ = 0;
    selected_ = 0;
    changed_ = 0;
    opened_ = 1;
    color_ = FL_BLACK;

    label_ = strdup(label);
    pixmap_ = pixmap;
    can_open_ = can_open;
    data_ = d;
  }

  char* label(void) {
    return label_;
  }

  void label(char* ptr) {
    if (label_)
      delete label_;
    label_ = strdup(ptr);
  }

  Fl_Pixmap* pixmap(void) {
    return pixmap_;
  }

  void pixmap(Fl_Pixmap* ptr) {
    pixmap_ = ptr;
  }

  void* data() const {
    return data_;
  }

  void data(void* v) {
    data_ = v;
  }

  int can_open() {
    return can_open_;
  }

  void can_open (int b) {
    can_open_ = b;
  }

  int is_open() {
    return opened_;
  }

  Fl_Color color() {
    return color_;
  }

  void color(Fl_Color b) {
    color_ = b;
  }

protected:

  int selected_;
  int changed_;
  int can_open_;
  Fl_Color color_;

  char* label_;
  Fl_Pixmap* pixmap_;
  void* data_;

};

#endif
