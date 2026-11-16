#ifndef Fl_Node_Base_H
#define Fl_Node_Base_H

class Fl_Toggle_Node_Base {

  friend class Fl_Toggle_Tree_Base;

public:

  // Nodes are deleted through Fl_Toggle_Node_Base pointers (Neo-PostOffice fix).
  virtual ~Fl_Toggle_Node_Base () {}
  Fl_Toggle_Node_Base () {
    prev_ = 0;
    next_ = 0;
    sub_ = 0;
    vsub_ = 0;
    up_ = 0;
    opened_ = 0;
  }

  // Neo-PostOffice: read access for keeping the open state across rebuilds.
  int is_open (void) const { return opened_; }
  Fl_Toggle_Node_Base* next_node (void) const { return next_; }

protected:

  Fl_Toggle_Node_Base* prev_;
  Fl_Toggle_Node_Base* next_;
  Fl_Toggle_Node_Base* sub_;
  Fl_Toggle_Node_Base* vsub_;
  Fl_Toggle_Node_Base* up_;

  int opened_;

};

#endif
