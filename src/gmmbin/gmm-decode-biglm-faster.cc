// gmmbin/gmm-decode-faster.cc

// Copyright 2009-2011  Microsoft Corporation

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


#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "gmm/am-diag-gmm.h"
#include "hmm/transition-model.h"
#include "fstext/fstext-lib.h"
#include "decoder/biglm-faster-decoder.h"
#include "decoder/decodable-am-diag-gmm.h"
#include "util/timer.h"

namespace kaldi {
void ReverseFeatures(Matrix<BaseFloat> *feats) {
  Vector<BaseFloat> tmp(feats->NumCols());
  for (size_t i = 0; i < feats->NumRows()/2; i++) {
    size_t j = feats->NumRows() - i - 1;  // mirror-image of i.
    tmp.CopyRowFromMat(*feats, i);
    feats->Row(i).CopyRowFromMat(*feats, j);
    feats->Row(j).CopyFromVec(tmp);
  }
}
}


int main(int argc, char *argv[])
{
  try {
    using namespace kaldi;
    typedef kaldi::int32 int32;
    using fst::SymbolTable;
    using fst::VectorFst;
    using fst::StdArc;

    const char *usage =
        "Decode features using GMM-based model.\n"
        "Usage:  gmm-decode-faster [options] model-in fst-in gminus-fst-in gprime-fst-in features-rspecifier words-wspecifier [alignments-wspecifier]\n";
    ParseOptions po(usage);
    bool time_reversed = false;
    BaseFloat acoustic_scale = 0.1;

    std::string word_syms_filename;
    BigLmFasterDecoderOptions decoder_opts;
    decoder_opts.Register(&po, true);  // true == include obscure settings.
    po.Register("time-reversed", &time_reversed, "If true, decode backwards in time [requires reversed graph.]\n");
    po.Register("acoustic-scale", &acoustic_scale, "Scaling factor for acoustic likelihoods");

    po.Register("word-symbol-table", &word_syms_filename, "Symbol table for words [for debug output]");

    po.Read(argc, argv);

    if (po.NumArgs() < 6 || po.NumArgs() > 7) {
      po.PrintUsage();
      exit(1);
    }

    std::string model_in_filename = po.GetArg(1),
        fst_in_filename = po.GetArg(2),
        gminus_fst_in_filename = po.GetArg(3),
        gprime_fst_in_filename = po.GetArg(4),
        feature_rspecifier = po.GetArg(5),
        words_wspecifier = po.GetArg(6),
        alignment_wspecifier = po.GetOptArg(7);

    TransitionModel trans_model;
    AmDiagGmm am_gmm;
    {
      bool binary;
      Input is(model_in_filename, &binary);
      trans_model.Read(is.Stream(), binary);
      am_gmm.Read(is.Stream(), binary);
    }

    Int32VectorWriter words_writer(words_wspecifier);

    Int32VectorWriter alignment_writer;
    if (alignment_wspecifier != "")
      if (!alignment_writer.Open(alignment_wspecifier))
        KALDI_ERR << "Failed to open alignments output.";

    fst::SymbolTable *word_syms = NULL;
    if (word_syms_filename != "") {
      word_syms = fst::SymbolTable::ReadText(word_syms_filename);
      if (!word_syms)
        KALDI_EXIT << "Could not read symbol table from file "<<word_syms_filename;
    }

    SequentialBaseFloatMatrixReader feature_reader(feature_rspecifier);

    // It's important that we initialize decode_fst after feature_reader, as it
    // can prevent crashes on systems installed without enough virtual memory.
    // It has to do with what happens on UNIX systems if you call fork() on a
    // large process: the page-table entries are duplicated, which requires a
    // lot of virtual memory.
    VectorFst<StdArc> *decode_fst = NULL;
    {
      std::ifstream is(fst_in_filename.c_str());
      if (!is.good()) KALDI_EXIT << "Could not open decoding-graph FST "
                                << fst_in_filename;
      decode_fst =
          VectorFst<StdArc>::Read(is, fst::FstReadOptions(fst_in_filename));
      if (decode_fst == NULL) // fst code will warn.
        exit(1);
    }

    VectorFst<StdArc> *gminus_fst = NULL;
    {
      std::ifstream is(gminus_fst_in_filename.c_str());
      if (!is.good()) KALDI_EXIT << "Could not open decoding-graph FST "
        << gminus_fst_in_filename;
      gminus_fst =
      VectorFst<StdArc>::Read(is, fst::FstReadOptions(gminus_fst_in_filename));
      if (gminus_fst == NULL) // fst code will warn.
        exit(1);
    }
    
    VectorFst<StdArc> *gprime_fst = NULL;
    {
      std::ifstream is(gprime_fst_in_filename.c_str());
      if (!is.good()) KALDI_EXIT << "Could not open decoding-graph FST "
        << gprime_fst_in_filename;
      gprime_fst =
      VectorFst<StdArc>::Read(is, fst::FstReadOptions(gprime_fst_in_filename));
      if (gprime_fst == NULL) // fst code will warn.
        exit(1);
    }

    BaseFloat tot_like = 0.0;
    kaldi::int64 frame_count = 0;
    int num_success = 0, num_fail = 0;
    fst::DeterministicOnDemandFst<fst::StdArc>* gdiff_fst = new fst::DeterministicOnDemandFst<fst::StdArc>(*gminus_fst,*gprime_fst);
    BigLmFasterDecoder decoder(*decode_fst, *gdiff_fst, decoder_opts);

    Timer timer;

    for (; !feature_reader.Done(); feature_reader.Next()) {
      std::string key = feature_reader.Key();
      Matrix<BaseFloat> features (feature_reader.Value());
      feature_reader.FreeCurrent();
      if (features.NumRows() == 0) {
        KALDI_WARN << "Zero-length utterance: " << key;
        num_fail++;
        continue;
      }
      if (time_reversed) ReverseFeatures(&features);

      DecodableAmDiagGmmScaled gmm_decodable(am_gmm, trans_model, features,
                                             acoustic_scale);
      decoder.Decode(&gmm_decodable);

      std::cerr << "Length of file is "<<features.NumRows()<<'\n';

      VectorFst<StdArc> decoded;  // linear FST.
      bool saw_endstate = decoder.GetOutput(true,  // consider only final states.
                                            &decoded);

      if (saw_endstate || decoder.GetOutput(false,
                                           &decoded)) {
        num_success++;
        if (!saw_endstate) {
          KALDI_WARN << "Decoder did not reach end-state, outputting partial traceback.";
        }
        std::vector<int32> alignment;
        std::vector<int32> words;
        StdArc::Weight weight;
        frame_count += features.NumRows();

        GetLinearSymbolSequence(decoded, &alignment, &words, &weight);

        if (time_reversed) { ReverseVector(&alignment);  ReverseVector(&words); } 

        words_writer.Write(key, words);
        if (alignment_writer.IsOpen())
          alignment_writer.Write(key, alignment);
        if (word_syms != NULL) {
          std::cerr << key << ' ';
          for (size_t i = 0; i < words.size(); i++) {
            std::string s = word_syms->Find(words[i]);
            if (s == "")
              KALDI_ERR << "Word-id " << words[i] <<" not in symbol table.";
            std::cerr << s << ' ';
          }
          std::cerr << '\n';
        }
        BaseFloat like = -weight.Value();
        tot_like += like;
        std::cerr << "Log-like per frame for utterance " << key << " is "
                  << (like / features.NumRows()) << "\n";

      } else {
        num_fail++;
        KALDI_WARN << "Did not successfully decode utterance " << key
                   << ", len = " << features.NumRows() << "\n";
      }
    }

    std::cerr << "Average log-likelihood per frame is " << (tot_like/frame_count) << " over "
              <<frame_count<<" frames.\n";

    double elapsed = timer.Elapsed();
    std::cerr << "Time taken [excluding initialization] "<< elapsed
              << "s: real-time factor assuming 100 frames/sec is "
              << (elapsed*100.0/frame_count) << '\n';
    std::cerr << "Succeeded for " << num_success << " utterances, failed for "
              << num_fail << '\n';

    delete decode_fst;
    delete gminus_fst;
    delete gprime_fst;
    delete gdiff_fst;
    if (num_success != 0) return 0;
    else return 1;
  } catch(const std::exception& e) {
    std::cerr << e.what();
    return -1;
  }
}


