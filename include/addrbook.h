#ifndef _ADDRBOOK_H
#define _ADDRBOOK_H 1
//===============================================================================
// Neo-PostOffice: the address book
// ---------------------------------
// Entries are kept in the settings under postoffice/contacts, one
// collection per entry (001, 002, ...) holding nickname, name and email,
// each encoded with encode_mysql. The old address book (nickname = address
// under postoffice/addressbook) is moved there the first time it is read.
//===============================================================================

#include <string>
#include <vector>
#include <FL/Fl_Input.H>

namespace addrbook
{
	struct Entry { std::string Id, Nickname, Name, Email; };

	// All entries, sorted by nickname, then name, then address.
	std::vector<Entry>	All (void);

	// Stores the entry (a new one when Id is empty) and returns its Id.
	std::string			Save (const Entry &);
	void				Remove (const std::string &id);

	// The entry with this address (case-insensitive), or NULL-Id entry.
	Entry				ByEmail (const std::string &email);

	// "Name <addr>" (the name quoted when it holds special characters), or
	// just the address when there is no name.
	std::string			Format (const Entry &);

	// Entries whose nickname, a word of the name or the address start with
	// the text (case-insensitive); at most max, nickname matches first.
	std::vector<Entry>	Match (const std::string &text, size_t max = 10);

	// A nickname suggested for a new entry: the first word of the name, or
	// the part of the address before the @, in lower case.
	std::string			SuggestNickname (const std::string &name, const std::string &email);
}

// A To/CC/BCC field that offers matching address book entries in a list
// under it while you type (after two letters of an address). Up and Down
// choose, Enter or Tab take the entry, Escape closes the list; a click on
// an entry takes it too.
class Fl_Address_Input : public Fl_Input
{
	public:
						 Fl_Address_Input (int x, int y, int w, int h, const char *l = 0);
						~Fl_Address_Input ();
		int				 handle (int) override;
		void			 hide () override;

		void			 Accept (int row);		// used by the list
		void			 ClosePopup (void);

	private:
		void			 UpdatePopup (void);
		bool			 Token (int &start, int &end, std::string &text);
		std::vector<addrbook::Entry> matches;
};

#endif
