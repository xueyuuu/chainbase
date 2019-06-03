#include "chainbase/chainbase.hpp"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <iostream>
#include <string>

using namespace chainbase;
using namespace boost::multi_index;

enum tables {
   book_table
};

/**
 * Defines a "table" for storing books. This table is assigned a
 * globally unique ID (book_table) and must inherit from chainbase::object<> which
 * decorates the book type by defining "id_type" and "type_id"
 */
struct book : public chainbase::object<book_table, book> {

   /** defines a default constructor for types that don't have
     * members requiring dynamic memory allocation.
     */
   CHAINBASE_DEFAULT_CONSTRUCTOR( book )
   id_type          id; ///< this manditory member is a primary key
   int pages        = 0;
   int publish_date = 0;
  //int a = 0;
  //int b = 0;
};

//struct by_id;
//struct by_pages;
//struct by_date;

/**
 * This is a relatively standard boost multi_index_container definition that has three
 * requirements to be used withn a chainbase database:
 *   - it must use chainbase::allocator<T>
 *   - the first index must be on the primary key (id) and must be unique (hashed or ordered)
 */
/*
typedef multi_index_container< 
     book, 
     indexed_by<
     ordered_unique< tag<by_id>, member<book,book::id_type,&book::id> >, ///< required
     ordered_non_unique< tag<by_pages>, BOOST_MULTI_INDEX_MEMBER(book,int,pages) >,
     ordered_non_unique< tag<by_date>, BOOST_MULTI_INDEX_MEMBER(book,int,publish_date) >
  >,
  chainbase::allocator<book> ///< required for use with chainbase::database
> book_index;
*/
typedef multi_index_container<
        book,
        indexed_by<
                ordered_unique<member<book, book::id_type, &book::id>>,
                ordered_non_unique<BOOST_MULTI_INDEX_MEMBER(book, int, pages)>,
                ordered_non_unique<BOOST_MULTI_INDEX_MEMBER(book, int, publish_date)>
        >,
        chainbase::allocator<book>
> book_index;

CHAINBASE_SET_INDEX_TYPE(book, book_index)

/**
    This simple program will open database_dir and add two new books every time
    it is run and then print out all of the books in the database.
 */
int main( int argc, char** argv ) {
   chainbase::database db;
   db.open( "database_dir", database::read_write, 1024*1024*8 ); /// open or create a database with 8MB capacity
   db.add_index<book_index>(); /// open or create the book_index


   const auto& book_idx = db.get_index<book_index>().indicies();
//BOOST_TEST_MESSAGE("Creating book");
   const auto &new_book300 = db.create<book>([](book &b) {
       b.pages = 3;
       b.publish_date = 11;
   });
   const auto &new_book400 = db.create<book>([](book &b) {
       b.pages = 40;
       b.publish_date = 12;
   });
   /**
      Returns a const reference to the book, this pointer will remain
      valid until the book is removed from the database.
    */
   db.modify( new_book300, [](book& b) {
      b.pages++;
   });

   for( const auto& b : book_idx ) {
      std::cout << b.pages << "\n";
   }
   db.remove( new_book400 );
   return 0;
}
