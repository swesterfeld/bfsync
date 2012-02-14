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
    PyErr_SetString (PyExc_Exception, ("BDB Exception: " + e.error_string()).c_str());
    return NULL;
  }
}

%include "bfsyncdb.hh"
