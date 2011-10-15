// fstbin/fstmakecontextsyms.cc

// Copyright 2009-2011 Microsoft Corporation

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#include "tree/context-dep.h"
#include "util/common-utils.h"
#include "fst/fstlib.h"
#include "fstext/fstext-utils.h"
#include "fstext/context-fst.h"


/*
    Test for this and makecontextfst:
    mkdir -p ~/tmpdir
    pushd ~/tmpdir
    (echo "<eps> 0"; echo "a 1"; echo "b 2" ) > phones.txt
    ( echo 3; echo 4 ) > disambig.list
    fstmakecontextfst --disambig-syms=disambig.list phones.txt 5 ilabels.sym > C.fst
    fstmakecontextsyms --disambig-syms=disambig.list phones.txt ilabels.sym > context_syms.txt
    cp phones.txt phones_disambig.txt;
     ( echo "#0 3"; echo "#1 4"; echo "$ 5" ) >> phones_disambig.txt
    fstrandgen C.fst | fstprint --isymbols=context_syms.txt --osymbols=phones_disambig.txt

    Example output:
    0   1   <eps>   a
    1   2   <eps>/a/<eps>   $
    2   3   #0  #0
    3
*/


int main(int argc, char *argv[]) {
  try {
    using namespace kaldi;
    using namespace fst;
    typedef fst::StdArc::Label Label;
    const char *usage =  "Create input symbols for CLG\n"
        "Usage: fstmakecontextsyms phones-symtab ilabels_input_file [output-symtab.txt]\n"
        "E.g.:  fstmakecontextsyms  phones.txt ilabels.sym > context_symbols.txt\n";

    ParseOptions po(usage);

    std::string disambig_list_file = "",
        phone_separator = "/",
        disambig_prefix = "#";

    po.Register("phone-separator", &phone_separator,
                "Separator for phones in phone-in-context symbols.");
    po.Register("disambig-prefix", &disambig_prefix,
                "Prefix for disambiguation symbols (if used).");

    po.Read(argc, argv);

    if (po.NumArgs() < 2 || po.NumArgs() > 3) {
      po.PrintUsage();
      exit(1);
    }

    std::string phones_symtab_filename = po.GetArg(1),
        ilabel_info_filename = po.GetArg(2),
        clg_symtab_filename = po.GetOptArg(3);

    std::vector<std::vector<kaldi::int32> > ilabel_info;
    {
      bool binary;
      Input ki(ilabel_info_filename, &binary);
      ReadILabelInfo(ki.Stream(),
                     binary, &ilabel_info);
    }

    fst::SymbolTable *phones_symtab = NULL;
    {  // read phone symbol table.
      std::ifstream is(phones_symtab_filename.c_str());
      phones_symtab = fst::SymbolTable::ReadText(is, phones_symtab_filename);
      if (!phones_symtab) KALDI_EXIT << "Could not read phones symbol-table file "<<phones_symtab_filename;
    }

    fst::SymbolTable *clg_symtab =
        CreateILabelInfoSymbolTable(ilabel_info,
                                    *phones_symtab,
                                    phone_separator,
                                    disambig_prefix);

    if (clg_symtab_filename == "") {
      if (!clg_symtab->WriteText(std::cout))
        KALDI_EXIT << "Cannot write symbol table to standard output.";
    } else {
      if (!clg_symtab->WriteText(clg_symtab_filename))
        KALDI_EXIT << "Cannot open symbol table file "<<clg_symtab_filename<<" for writing.";
    }
    delete clg_symtab;
    delete phones_symtab;
    return 0;
  } catch(const std::exception& e) {
    std::cerr << e.what();
    return -1;
  }
}

