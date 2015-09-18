#include <vector>
#include <memory>

namespace et {

struct Node {
  std::string fen_;
  double result_;
};

class PGNPlayer : public pgn::PGNPlayer
{
public:
  PGNPlayer(const char* path) : pgn::PGNPlayer(path), position_count_(0) {}

  virtual ~PGNPlayer() {}

  virtual void readPGNDatabase() {
    PGNFileReader::readPGNDatabase();
    printProgress(true);
  }

  virtual void readSANMove() {
    pgn::PGNPlayer::readSANMove();

    auto half_move_count = game_->pos - game_->position_list;

    auto stage = (game_->pos->material.value()-game_->pos->material.pawnValue())/
                  (double)game_->pos->material.max_value_without_pawns;

    if (stage <= .901 && half_move_count <= 60 && stage >= 0.199) {
      node_.fen_ = game_->getFen();
      game_nodes_.push_back(node_);
    }
    ++position_count_;
  }

  virtual void readGameTermination() {
    pgn::PGNPlayer::readGameTermination();

    for (auto& node : game_nodes_) {
      node.result_ = result_ == pgn::WhiteWin ? 1 : result_ == pgn::Draw ? 0.5 : 0;
    }
    all_nodes_.insert(all_nodes_.end(), game_nodes_.begin(), game_nodes_.end());
    game_nodes_.clear();

    printProgress(false);
  }

  virtual void readComment1() {
    pgn::PGNPlayer::readComment1();
    //cout << comment_ << endl;
  }

  void printProgress(bool force) {
    if (!force && game_count_ % 100 != 0) {
      return;
    }
    cout << "game_count_:" << game_count_
    << " position_count_:" << position_count_
    << " all_nodes_.size:" << all_nodes_.size()
    << endl;
  }

  std::vector<Node> all_nodes_;

private:
  std::vector<Node> game_nodes_;
  Node node_;
  int64_t position_count_;
};

struct Param {
  Param(std::string name, int& value, int initial_value, int step) :
    name_(name), value_(value), step_(step)
  {
    value = initial_value;
  }
  Param(std::string name, int& value, int step) :
    name_(name), value_(value), step_(step)
  {
  }
  std::string name_;
  int& value_;
  int step_;
};

class Tune : public MoveSorter {
public:
  Tune(Game& game, See& see, Eval& eval) : game_(game), see_(see), eval_(eval) {
    PGNPlayer pgn("C:\\chess\\lb\\test3.pgn");

    pgn.read();

    // Tune as described in https://chessprogramming.wikispaces.com/Texel%27s+Tuning+Method

    std::vector<Param> params;

    params.push_back(Param("pawn_isolated_open_mg", eval_.pawn_isolated_open_mg, 3));
    params.push_back(Param("pawn_isolated_mg", eval_.pawn_isolated_mg, 3));
    params.push_back(Param("pawn_isolated_open_eg", eval_.pawn_isolated_open_eg, 3));
    params.push_back(Param("pawn_isolated_eg", eval_.pawn_isolated_eg, 3));
    params.push_back(Param("pawn_unsupported_open_mg", eval_.pawn_unsupported_open_mg, 3));
    params.push_back(Param("pawn_unsupported_mg", eval_.pawn_unsupported_mg, 3));
    params.push_back(Param("pawn_unsupported_open_eg", eval_.pawn_unsupported_open_eg, 3));
    params.push_back(Param("pawn_unsupported_eg", eval_.pawn_unsupported_eg, 3));
    params.push_back(Param("pawn_doubled_mg", eval_.pawn_doubled_mg, 3));
    params.push_back(Param("pawn_doubled_eg", eval_.pawn_doubled_eg, 3));
    /*
    params.push_back(Param("bishop_pair_mg", eval_.bishop_pair_mg, 3));
    params.push_back(Param("bishop_pair_eg", eval_.bishop_pair_eg, 3));
    params.push_back(Param("rook_on_open_mg", eval_.rook_on_open_mg, 3));
    params.push_back(Param("rook_on_open_eg", eval_.rook_on_open_eg, 3));
    params.push_back(Param("rook_on_half_open_mg", eval_.rook_on_half_open_mg, 3));
    params.push_back(Param("rook_on_half_open_eg", eval_.rook_on_half_open_eg, 3));
    params.push_back(Param("side_to_move_mg", eval_.side_to_move_mg, 3));
    params.push_back(Param("side_to_move_eg", eval_.side_to_move_eg, 3));
    params.push_back(Param("pawn_advance_mg", eval_.pawn_advance_mg, 1));
    params.push_back(Param("pawn_advance_eg", eval_.pawn_advance_eg, 1));
    for (auto i = 0; i < 14; ++i) params.push_back(Param("bishop_mob_mg", eval_.bishop_mob_mg[i], 1));
    for (auto i = 0; i < 9; ++i) params.push_back(Param("knight_mob_mg", eval_.knight_mob_mg[i], 1));
    for (auto i = 0; i < 15; ++i) params.push_back(Param("rook_mob_mg", eval_.rook_mob_mg[i], 2));
    for (auto i = 0; i < 15; ++i) params.push_back(Param("rook_mob_eg", eval_.rook_mob_eg[i], 2));
    */
    double K = bestK();
    double bestE = E(pgn.all_nodes_, params, K);
    bool improved = true;

    while (improved) {
      printBestValues(bestE, params);
      improved = false;

      for (size_t i = 0; i < params.size(); ++i) {
        auto& step = params[i].step_;
        auto& value = params[i].value_;

        if (step == 0) {
          continue;
        }
        value += step;

        double newE = E(pgn.all_nodes_, params, K);

        if (newE < bestE) {
          bestE = newE;
          improved = true;
        }
        else if (step > 0) {
          step = -step;
          value += 2*step;
          newE = E(pgn.all_nodes_, params, K);

          if (newE < bestE) {
            bestE = newE;
            improved = true;
          }
          else {
            value -= step;
            step = 0;
          }
        }
        else {
            value -= step;
            step = 0;
        }
      }
    }
    printBestValues(bestE, params);
  }

  virtual ~Tune() {}

  double E(const std::vector<Node>& nodes, const std::vector<Param>& param_values, double K) {
    double x = 0;

    for (auto node : nodes) {
      game_.setFen(node.fen_.c_str());
      x += pow(node.result_ - sigmoid(getScore(0), K), 2);
    }
    x /= nodes.size();

    cout << "x:" << x;
    for (size_t i = 0; i < param_values.size(); ++i) {
      if (param_values[i].step_) {
        cout << " prm[" << i << "]:" << param_values[i].value_;
      }
    }
    cout << endl;
    return x;
  }

  Score getScore(int side) {
    Score score = getQuiesceScore(-32768, 32768);
    return game_.pos->side_to_move == side ? score : -score;
  }

  Score getQuiesceScore(Score alpha, const Score beta) {
    auto score = eval_.evaluate(-100000, 100000);

    if (score >= beta) {
      return score;
    }
    auto best_score = score;

    if (best_score > alpha) {
      alpha = best_score;
    }
    game_.pos->generateCapturesAndPromotions(this);

    while (const auto move_data = game_.pos->nextMove()) {
      if (!isPromotion(move_data->move)) {
        if (move_data->score < 0) {
          break;
        }
      }

      if (game_.makeMove(move_data->move, true, true)) {
        auto score = -getQuiesceScore(-beta, -alpha);

        game_.unmakeMove();

        if (score > best_score) {
          best_score = score;

          if (best_score > alpha) {
            if (score >= beta) {
              break;
            }
            alpha = best_score;
          }
        }
      }
    }
    return best_score;
  }

  virtual void sortMove(MoveData& move_data) {
    const auto m = move_data.move;

    if (isQueenPromotion(m)) {
      move_data.score = 890000;
    }
    else if (isCapture(m)) {
      auto value_captured = piece_value(moveCaptured(m));
      auto value_piece = piece_value(movePiece(m));

      if (value_piece == 0) {
        value_piece = 1800;
      }
      if (value_piece <= value_captured) {
        move_data.score = 300000 + value_captured*20 - value_piece;
      }
      else if (see_.seeMove(m) >= 0) {
        move_data.score = 160000 + value_captured*20 - value_piece;
      }
      else {
        move_data.score = -100000 + value_captured*20 - value_piece;
      }
    }
    else {
      exit(0);
    }
  }

  void printBestValues(double E, const std::vector<Param> params) {
    auto finished = 0;
    for (size_t i = 0; i < params.size(); ++i) {
      if (params[i].step_ == 0) {
          finished++;
      }
      cout << i << ":"
      << params[i].name_ << ":" << params[i].value_
      << "  step:" << params[i].step_
      << endl;
    }
    cout << "Best E:" << E << "  " << endl;
    cout << "Finished:" << finished*100.0/params.size() << "%" << endl;
  }

  double bestK() {
    /*
    double smallestE;
    double bestK = -1;
    for (double K = 1.10; K < 1.15; K += 0.01) {
        double _E = E(pgn.all_nodes_, params, K);
        if (bestK < 0 || _E < smallestE) {
          bestK = K;
          smallestE = _E;
        }
        std::cout << "K:" << K << "  _E:" << _E << endl;
    }
    std::cout << "bestK:" << bestK << "smallestE:" << smallestE << endl;
    */
    return 1.12;
  }

private:
  Game& game_;
  See& see_;
  Eval& eval_;
};

}
