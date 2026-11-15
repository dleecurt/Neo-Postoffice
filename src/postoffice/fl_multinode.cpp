class Fl_MultiNode
{
	public:
						 Fl_MultiNode (void);
						 Fl_MultiNode (char *);
						 Fl_MultiNode (Fl_MultiNode *);
						 Fl_MultiNode (Fl_MultiNode *, char *);
						~Fl_MultiNode (void);
	
		Fl_MultiNode	*Find (char *);
		void			 Find (unsigned long);
		Fl_MultiNode	*Create (char *);
	
		Fl_MultiNode	*Next, *Prev;
		Fl_MultiNode	*First, *Last;
		Fl_MultiNode	*Parent;
		
		char			*Ident;
		unsigned long	 Checksum;
		WordList		*Fields;
		bool			 Opened;
		int				 Count;
};

Fl_MultiNode::Fl_MultiNode (void)
{
	Next = Prev = NULL;
	First = Last = NULL;
	Parent = NULL;
	Ident = NULL;
	Checksum = 0;
	Fields = new WordList;
	Opened = false;
	Count = 0;
}

Fl_MultiNode::Fl_MultiNode (char *ident)
{
	Ident = strdupp (ident);
	Checksum = checksum (ident);
	Fields = new WordList;
	Opened = false;
	Next = Prev = NULL;
	First = Last = NULL;
	Parent = NULL;
	Count = 0;
}

Fl_MultiNode::Fl_MultiNode (Fl_MultiNode *parent)
{
	Parent = parent;
	Next = Prev = NULL;
	First = Last = NULL;
	Ident = NULL;
	Checksum = 0;
	Count = 0;
	Fields = new WordList;
	Opened = false;
	
	if (Parent->Last)
	{
		Prev = Parent->Last;
		Parent->Last->Next = this;
		Parent->Last = this;
		Parent->Count++;
	}
	else
	{
		Parent->Last = Parent->First = this;
		Parent->Count = 1;
	}
}

Fl_MultiNode::Fl_MultiNode (Fl_MultiNode *parent, char *ident)
{
	Ident = strdupp (ident);
	Checksum = checksum (ident);
	Parent = parent;
	Next = Prev = NULL;
	First = Last = NULL;
	Count = 0;
	Fields = new WordList;
	Opened = false;
	
	if (Parent->Last)
	{
		Prev = Parent->Last;
		Parent->Last->Next = this;
		Parent->Last = this;
		Parent->Count++;
	}
	else
	{
		Parent->Last = Parent->First = this;
		Parent->Count = 1;
	}
}

Fl_MultiNode::~Fl_MultiNode (void)
{
	Fl_MultiNode *N, *NN;
	N = First;
	
	while (N)
	{
		NN = N->Next;
		delete (N);
		N = NN;
	}
	
	if (Ident) delete[] (Ident);
	delete (Fields);
}

Fl_MultiNode *Fl_MultiNode::Find (char *ident)
{
	Fl_MultiNode *N, *res;
	unsigned long chk = checksum (ident);
	if (chk == Checksum) return this;
	N = First;
	while (N)
	{
		if (res = N->Find (chk)) return res;
		N = N->Next;
	}
}

Fl_MultiNode *Fl_MultiNode::Find (unsigned long chk)
{
	Fl_MultiNode *N, *res;
	if (chk == Checksum) return this;
	N = First;
	while (N)
	{
		if (res = N->Find (chk)) return res;
		N = N->Next;
	}
}

Fl_MultiNode *Fl_MultiNode::Create (char *par)
{
	Fl_MultiNode *N = Find (par);
	if (!N) N = this;
	return new Fl_MultiNode (N);
}

