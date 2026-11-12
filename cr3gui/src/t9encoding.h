#ifndef __T9ENCODIG
#define __T9ENCODIG 1

#include "lvstring.h"
#include "lvarray.h"

// T9-like encoding table
class TEncoding {
    lString32Collection keytable_;
public:

	void set( const lString32Collection & items )
	{
		keytable_.clear();
		keytable_.addAll( items );
	}

    int length() const { return keytable_.length(); }

    const lString32 & operator [] ( int index ) const { return keytable_[index]; }

    TEncoding() { }

    TEncoding( const lChar32 ** defs )
    {
        init( defs );
    }

    TEncoding(const TEncoding& other) : keytable_(other.keytable_) {}

    virtual ~TEncoding() {}

    void init(  const lChar32 ** defs )
    {
        keytable_.clear();
        for (; *defs; defs++ ) {
            lString32 s( *defs );
            assert(keytable_.length() <= 10);
            keytable_.add( s );
        }
    }

    int encode(lChar32 ch) const
    {
        assert(keytable_.length() <= 10);
        for (int i = 0; i < keytable_.length(); i++) {
            const lString32& ref = keytable_[i];
            for( int j = 0; j < ref.length(); j ++ ) {
                if (ref[j] == ch) {
                    return i;
                }
            }
        }
        return 0;
    }

    lString8
    encode_string( lString32 s ) const
    { // s not const, because we lower it here
        s.lowercase();
        lString8 result;
        for (int i = 0; i < s.length(); i ++) {
            result.append(1,static_cast<lChar8>('0'+encode(s[i])));
        }
        return result;
    }

protected:
   void defkey(const lChar32 *chars) {
        assert(keytable_.length() <= 10);
        keytable_.add(lString32(chars));
   }
};

class T9ClassicEncoding : public TEncoding {
       //T9 drawn on my Siemens S55 ;)
public:
   T9ClassicEncoding () : TEncoding() {
       defkey(U".,"); // 0 STUB
       defkey(U" "); // 1 are STUBs
       defkey(U"abc"); // 2
       defkey(U"def"); // 3
       defkey(U"ghi"); // 4
       defkey(U"jkl"); // 5
       defkey(U"mno"); // 6
       defkey(U"pqrs"); // 7
       defkey(U"tuv"); // 8
       defkey(U"wxyz"); // 9
    }
};

class T9Encoding : public TEncoding {
   // T9 by LV
public:
   T9Encoding () : TEncoding() {
       defkey(U" .,"); // 0 STUB
       defkey(U"abc"); // 1
       defkey(U"def"); // 2
       defkey(U"ghi"); // 3
       defkey(U"jkl"); // 4
       defkey(U"mno"); // 5
       defkey(U"pqrs"); // 6
       defkey(U"tuv"); // 7
       defkey(U"wxyz"); // 8
       defkey(U""); // 9 are STUBs
    }
};
#endif
