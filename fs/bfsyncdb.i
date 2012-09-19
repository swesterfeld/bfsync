/* bfsyncdb.i */
%module bfsyncdb
%{
#include "bfsyncdb.hh"
%}

%include "std_string.i"
%include "std_vector.i"
%include "stdint.i"

// Instantiate templates used
namespace std {
  %template(StringVector) vector<string>;
  %template(LinkVector) vector<Link>;
  %template(INodeVector) vector<INode>;
  %template(UIntVector) vector<unsigned int>;
  %template(TempFileVector) vector<TempFile>;
  %template(JournalEntryVector) vector<JournalEntry>;
}

%exception {
  try {
    $action
  }
  catch (BDBException& e) {
    if (e.error() != BFSync::BDB_ERROR_INTR)
      PyErr_SetString (PyExc_Exception, ("BDB Exception: " + e.error_string()).c_str());

    // INTR error is already handled by python signal handling code
    return NULL;
  }
}

%include "bfsyncdb.hh"
