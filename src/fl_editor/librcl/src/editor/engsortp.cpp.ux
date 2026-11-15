#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

RCLDLL void SortPositions(EditPosition& first, EditPosition& last)
{
	long temprow, tempcol;

	if(first.Row == last.Row)
		{
			if(last.Column < first.Column)  // if on same row, just swap columns
				{
					tempcol = first.Column;
					first.Column = last.Column;
					last.Column = tempcol;
				}
		}
	else if(last.Row < first.Row)        // else must swap both
		{
			temprow = first.Row;
			tempcol = first.Column;
			first.Row = last.Row;
			first.Column = last.Column;
			last.Row = temprow;
			last.Column = tempcol;
		}
}

