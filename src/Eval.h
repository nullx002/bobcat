/*
  This file is part of Bobcat.
  Copyright 2008-2011 Gunnar Harms

  Bobcat is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Bobcat is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Bobcat.  If not, see <http://www.gnu.org/licenses/>.
*/

class Eval {
public:
  Eval(Game* game, PawnStructureTable* pawnt, See* see) {
    initialise(game, pawnt, see);
  }

  virtual ~Eval() {
  }

  virtual void newGame() {
  }

  int evaluate(int alpha, int beta) {
    initialiseEvaluate();

    evalMaterialOneSide(0);
    evalMaterialOneSide(1);

    int mat_eval = mateval[0] - mateval[1];

    int lazy_margin = 300;
    int lazy_eval = pos->side_to_move == 0 ? mat_eval : -mat_eval;

    if (lazy_eval - lazy_margin > beta || lazy_eval + lazy_margin < alpha) {
      return pos->material.evaluate(pos->flags, lazy_eval, pos->side_to_move, board);
    }
    // Pass 1.
    evalPawnsBothSides();
    evalKnightsOneSide(0);
    evalKnightsOneSide(1);
    evalBishopsOneSide(0);
    evalBishopsOneSide(1);
    evalRooksOneSide(0);
    evalRooksOneSide(1);
    evalQueensOneSide(0);
    evalQueensOneSide(1);
    evalKingOneSide(0);
    evalKingOneSide(1);

    for (Side side = 0; side < 2; side++) {
      if (pawn_attacks[side] & king_area[side^1]) {
        attack_count[side]++;
        attack_counter[side] += 2;
      }
    }
    // Pass 2.
    for (Side side = 0; side < 2; side++) {
      evalPassedPawnsOneSide(side);
      evalKingAttackOneSide(side);
    }
    double stage = (pos->material.value()-pos->material.pawnValue())/
                   (double)pos->material.max_value_without_pawns;

    poseval_mg[pos->side_to_move] += side_to_move_mg;
    poseval_eg[pos->side_to_move] += side_to_move_eg;

    int pos_eval_mg = (int)((poseval_mg[0]-poseval_mg[1])*stage);
    int pos_eval_eg = (int)((poseval_eg[0]-poseval_eg[1])*(1-stage));
    int pos_eval = pos_eval_mg + pos_eval_eg + (poseval[0] - poseval[1]);
    int eval = pos_eval + mat_eval;

    return pos->material.evaluate(pos->flags, pos->side_to_move == 1 ? -eval : eval,
                                  pos->side_to_move, board);
  }

protected:
  __forceinline void evalPawnsBothSides() {
    pawnp = 0;

    if (pos->material.pawnCount()) {
      pawnp = pawnt->find(pos->pawn_structure_key);

      if (!pawnp) {
        pawn_eval_mg[0] = pawn_eval_mg[1] = 0;
        pawn_eval_eg[0] = pawn_eval_eg[1] = 0;

        passed_pawn_files[0] = passed_pawn_files[1] = 0;

        evalPawnsOneSide(0);
        evalPawnsOneSide(1);

        pawnp = pawnt->insert(pos->pawn_structure_key, (int)(pawn_eval_mg[0] - pawn_eval_mg[1]),
                              (int)(pawn_eval_eg[0] - pawn_eval_eg[1]), passed_pawn_files);
      }
      poseval_mg[0] += pawnp->eval_mg;
      poseval_eg[0] += pawnp->eval_eg;
    }
  }

  __forceinline void evalPawnsOneSide(const Side us) {
    const Side them = us ^ 1;
    int score_mg = 0;
    int score_eg = 0;

    for (BB bb = pawns(us); bb; ) {
      Square sq = lsb(bb);

      if (board->isPawnPassed(sq, us)) {
        passed_pawn_files[us] |= 1 << file(sq);
      }
      bool open_file = !board->isPieceOnFile(Pawn, sq, them);

      if (board->isPawnIsolated(sq, us)) {
        score_mg += open_file ? pawn_isolated_open_mg : pawn_isolated_mg;
        score_eg += open_file ? pawn_isolated_open_eg : pawn_isolated_eg;
      }
      else if ((bbSquare(sq) & pawn_attacks[us]) == 0) {
        score_mg += open_file ? pawn_unsupported_open_mg : pawn_unsupported_mg;
        score_eg += open_file ? pawn_unsupported_open_eg : pawn_unsupported_eg;;
      }

      resetLSB(bb);

      if (bbFile(sq) & bb) {
        score_mg += pawn_doubled_mg;
        score_eg += pawn_doubled_eg;
      }
      int r = us == 0 ? rank(sq) - 1 : 6 - rank(sq);
      score_mg += pawn_advance_mg*r;
      score_eg += pawn_advance_eg*r;
    }
    pawn_eval_mg[us] += score_mg;
    pawn_eval_eg[us] += score_eg;
  }

  __forceinline void evalKnightsOneSide(const Side us) {
    const Side them = us ^ 1;
    int score_mg = 0;
    int score_eg = 0;
    int score = 0;

    for (BB knights = board->knights(us); knights; resetLSB(knights)) {
      Square sq = lsb(knights);
      Square flipsq = flip[us][sq];

      score_mg += knight_pcsq_mg[flipsq];
      score_eg += knight_pcsq_eg[flipsq];

      const BB& attacks = knight_attacks[sq];
      int x = popCount(attacks & ~board->occupied_by_side[us] & ~pawn_attacks[them]);

      score += knight_mob_mg[x];

      all_attacks[us] |= attacks;
      _knight_attacks[us] |= attacks;

      bool outpost = (passed_pawn_front_span[us][sq] & (pawns(them) & ~pawn_front_span[us][sq])) == 0;

      if (outpost && (pawn_attacks[us] & bbSquare(sq))) {
        score += std::max(0, knight_pcsq_eg[flipsq]);
      }

      if (attacks & king_area[them]) {
        attack_counter[us] += popCount(attacks & king_area[them])*8;
        attack_count[us]++;
      }

      if (bbSquare(sq) & pawn_attacks[them]) {
        score += -28;
      }
    }
    poseval[us] += score;
    poseval_mg[us] += score_mg;
    poseval_eg[us] += score_eg;
  }

  __forceinline void evalBishopsOneSide(const Side us) {
    const Side them = us ^ 1;
    int score_mg = 0;
    int score_eg = 0;
    int score = 0;

    for (BB bishops = board->bishops(us); bishops; resetLSB(bishops)) {
      Square sq = lsb(bishops);
      const BB& bbsq = bbSquare(sq);
      Square flipsq = flip[us][sq];

      score_mg += bishop_pcsq_mg[flipsq];
      score_eg += bishop_pcsq_eg[flipsq];

      const BB attacks = Bmagic(sq, occupied);
      int x = popCount(attacks & ~(board->occupied_by_side[us]));

      score += bishop_mob_mg[x];

      all_attacks[us] |= attacks;
      bishop_attacks[us] |= attacks;

      if (bishop_trapped_a7h7[us] & bbsq) {
        int x = file(sq)/7;
        if ((pawns(them) & pawns_trap_bishop_a7h7[x][us]) == pawns_trap_bishop_a7h7[x][us]) {
          score -= 110;
        }
      }

      if (attacks & king_area[them]) {
        attack_counter[us] += popCount(attacks & king_area[them])*6;
        attack_count[us]++;
      }

      if (bbSquare(sq) & pawn_attacks[them]) {
        score += -28;
      }
    }
    poseval[us] += score;
    poseval_mg[us] += score_mg;
    poseval_eg[us] += score_eg;
  }

  __forceinline void evalRooksOneSide(const Side us) {
    const Side them = us ^ 1;
    int score_mg = 0;
    int score_eg = 0;
    int score = 0;

    for (BB rooks = board->rooks(us); rooks; resetLSB(rooks)) {
      Square sq = lsb(rooks);
      const BB& bbsq = bbSquare(sq);

      if (bbsq & open_files) {
        score_mg += rook_on_open_mg;
        score_eg += rook_on_open_eg;

        if (~bbsq & bbFile(sq) & board->rooks(us)) {
          score += 20;
        }
      }
      else if (bbsq & half_open_files[us]) {
        score_mg += rook_on_half_open_mg;
        score_eg += rook_on_half_open_eg;

        if (~bbsq & bbFile(sq) & board->rooks(us)) {
          score += 10;
        }
      }

      if ((bbsq & rank_7[us]) && (rank_7_and_8[us] & (pawns(them) | board->king(them)))) {
        score += 20;
      }
      const BB attacks = Rmagic(sq, occupied);
      int x = popCount(attacks & ~board->occupied_by_side[us]);

      score_mg += rook_mob_mg[x];
      score_eg += rook_mob_eg[x];

      all_attacks[us] |= attacks;
      rook_attacks[us] |= attacks;

      if (attacks & king_area[them]) {
        attack_counter[us] += popCount(attacks & king_area[them])*12;
        attack_count[us]++;
      }

      if (bbSquare(sq) & (pawn_attacks[them] | _knight_attacks[them] | bishop_attacks[them])) {
        score += -36;
      }
    }
    poseval[us] += score;
    poseval_mg[us] += score_mg;
    poseval_eg[us] += score_eg;
  }

  __forceinline void evalQueensOneSide(const Side us) {
    const Side them = us ^ 1;
    int score_mg = 0;
    int score_eg = 0;
    int score = 0;

    for (BB queens = board->queens(us); queens; resetLSB(queens)) {
      Square sq = lsb(queens);
      const BB& bbsq = bbSquare(sq);

      if ((bbsq & rank_7[us]) && (rank_7_and_8[us] & (pawns(them) | board->king(them)))) {
        score += 20;
      }
      const BB attacks = Qmagic(sq, occupied);

      all_attacks[us] |= attacks;
      queen_attacks[us] |= attacks;

      if (attacks & king_area[them]) {
        attack_counter[us] += popCount(attacks & king_area[them])*24;
        attack_count[us]++;
      }

      if (bbSquare(sq) & (pawn_attacks[them] | _knight_attacks[them] | bishop_attacks[them] | rook_attacks[them])) {
        score += -40;
      }
      else {
        score += 6*(7 - distance[sq][kingSq(them)]);
      }
    }
    poseval[us] += score;
    poseval_mg[us] += score_mg;
    poseval_eg[us] += score_eg;
  }

  __forceinline void evalMaterialOneSide(const Side us) {
    mateval[us] = pos->material.material_value[us];

    if (pos->material.count(us, Bishop) == 2) {
      poseval_mg[us] += bishop_pair_mg;
      poseval_eg[us] += bishop_pair_eg;
    }
  }

  __forceinline void evalKingOneSide(const Side us) {
    const Side them = us ^ 1;
    Square sq = lsb(board->king(us));
    const BB& bbsq = bbSquare(sq);

    int score_mg = king_pcsq_mg[flip[us][sq]];
    int score_eg = king_pcsq_eg[flip[us][sq]];

    score_mg += -45 + 15*popCount((pawnPush[us](bbsq) | pawnWestAttacks[us](bbsq) |
                                   pawnEastAttacks[us](bbsq)) & pawns(us));

    if (board->queens(them) || popCount(board->rooks(them)) > 1) {
      BB eastwest = bbsq | westOne(bbsq) | eastOne(bbsq);
      int x = -15*popCount(open_files & eastwest);
      int y = -10*popCount(half_open_files[us] & eastwest);

      score_mg += x;
      score_mg += y;

      if (board->queens(them) && popCount(board->rooks(them))) {
        score_mg += x;
        score_mg += y;

        if (popCount(board->rooks(them) > 1)) {
          score_mg += x;
          score_mg += y;
        }
      }
    }

    if (((us == 0) &&
         (((sq == f1 || sq == g1) && (bbSquare(h1) & board->rooks(0))) ||
          ((sq == c1 || sq == b1) && (bbSquare(a1) & board->rooks(0))))) ||
        ((us == 1) &&
         (((sq == f8 || sq == g8) && (bbSquare(h8) & board->rooks(1))) ||
          ((sq == c8 || sq == b8) && (bbSquare(a8) & board->rooks(1))))))
    {
      score_mg += -80;
    }

    all_attacks[us] |= king_attacks[kingSq(us)];
    poseval_mg[us] += score_mg;
    poseval_eg[us] += score_eg;
  }

  __forceinline void evalPassedPawnsOneSide(const Side us) {
    const Side them = us ^ 1;
    for (BB files = pawnp ? pawnp->passed_pawn_files[us] : 0; files; resetLSB(files)) {
      for (BB bb = bbFile(lsb(files)) & pawns(us); bb; resetLSB(bb)) {
        int sq = lsb(bb);
        const BB& front_span = pawn_front_span[us][sq];
        int r = us == 0 ? rank(sq) : 7 - rank(sq);
        int rr = r*r;

        int score_mg = passed_pawn_mg[r];
        int score_eg = passed_pawn_eg[r];

        score_eg += rr*(front_span & board->occupied_by_side[us] ? 0 : 1);
        score_eg += rr*(front_span & board->occupied_by_side[them] ? 0 : 1);
        score_eg += rr*(front_span & all_attacks[them] ? 0 : 1);
        score_eg += r*(distance[sq][kingSq(them)]*2-distance[sq][kingSq(us)]*2);

        poseval_mg[us] += score_mg;
        poseval_eg[us] += score_eg;
      }
    }
  }

  __forceinline void evalKingAttackOneSide(const Side side) {
    if (attack_count[side] > 1) {
      poseval_mg[side] += attack_counter[side]*(attack_count[side]-1);
    }
  }

  __forceinline const BB& pawns(Side side) {
    return board->pawns(side);
  }

  __forceinline Square kingSq(Side side) {
    return board->king_square[side];
  }

  __forceinline void initialiseEvaluate() {
    pos = game->pos;
    pos->flags = 0;

    poseval_mg[0] = poseval_eg[0] = poseval[0] = 0;
    poseval_mg[1] = poseval_eg[1] = poseval[1] = 0;

    attack_counter[0] = attack_counter[1] = 0;
    attack_count[0] = attack_count[1] = 0;

    king_area[0] = king_attacks[kingSq(0)] | board->king(0);
    king_area[1] = king_attacks[kingSq(1)] | board->king(1);

    occupied = board->occupied;
    not_occupied = ~occupied;

    open_files = ~(northFill(southFill(pawns(0))) | northFill(southFill(pawns(1))));
    half_open_files[0] = ~northFill(southFill(pawns(0))) & ~open_files;
    half_open_files[1] = ~northFill(southFill(pawns(1))) & ~open_files;

    all_attacks[0] = pawn_attacks[0] = pawnEastAttacks[0](pawns(0)) | pawnWestAttacks[0](pawns(0));
    all_attacks[1] = pawn_attacks[1] = pawnEastAttacks[1](pawns(1)) | pawnWestAttacks[1](pawns(1));

    _knight_attacks[0] = _knight_attacks[1] = 0;
    bishop_attacks[0] = bishop_attacks[1] = 0;
    rook_attacks[0] = rook_attacks[1] = 0;
    queen_attacks[0] = queen_attacks[1] = 0;
  }

  void initialise(Game* game, PawnStructureTable* pawnt, See* see) {
    this->game = game;
    board = game->pos->board;
    this->pawnt = pawnt;
    this->see = see;
  }

  Board* board;
  Position* pos;
  Game* game;
  PawnStructureTable* pawnt;
  See* see;
  PawnEntry* pawnp;

  int poseval_mg[2];
  int poseval_eg[2];
  int poseval[2];
  int mateval[2];
  int pawn_eval_mg[2];
  int pawn_eval_eg[2];
  int passed_pawn_files[2];
  int attack_counter[2];
  int attack_count[2];

  BB pawn_attacks[2];
  BB all_attacks[2];
  BB _knight_attacks[2];
  BB bishop_attacks[2];
  BB rook_attacks[2];
  BB queen_attacks[2];
  BB king_area[2];
  BB occupied;
  BB not_occupied;
  BB open_files;
  BB half_open_files[2];

public:
  static int knight_pcsq_mg[64];
  static int knight_pcsq_eg[64];
  static int bishop_pcsq_mg[64];
  static int bishop_pcsq_eg[64];
  static int king_pcsq_mg[64];
  static int king_pcsq_eg[64];

  static int knight_mob_mg[9];
  static int bishop_mob_mg[14];
  static int rook_mob_mg[15];
  static int rook_mob_eg[15];

  static int passed_pawn_mg[8];
  static int passed_pawn_eg[8];

  static int pawn_isolated_open_mg;
  static int pawn_isolated_mg;
  static int pawn_isolated_open_eg;
  static int pawn_isolated_eg;
  static int pawn_unsupported_open_mg;
  static int pawn_unsupported_mg;
  static int pawn_unsupported_open_eg;
  static int pawn_unsupported_eg;
  static int pawn_doubled_eg;
  static int pawn_doubled_mg;
  static int pawn_advance_mg;
  static int pawn_advance_eg;
  static int side_to_move_mg;
  static int side_to_move_eg;
  static int rook_on_open_mg;
  static int rook_on_open_eg;
  static int rook_on_half_open_mg;
  static int rook_on_half_open_eg;
  static int bishop_pair_eg;
  static int bishop_pair_mg;

  static BB bishop_trapped_a7h7[2];
  static BB pawns_trap_bishop_a7h7[2][2];
};

BB Eval::bishop_trapped_a7h7[2] = {
  (BB)1 << a7 | (BB)1 << h7, (BB)1 << a2 | (BB)1 << h2
};

BB Eval::pawns_trap_bishop_a7h7[2][2] = {
  { (BB)1 << b6 | (BB)1 << c7, (BB)1 << b3 | (BB)1 << c2 },
  { (BB)1 << g6 | (BB)1 << f7, (BB)1 << g3 | (BB)1 << f2 }
};

int Eval::knight_pcsq_mg[64] = {
 -45, -15,  -5,   5,   5,  -5, -15, -45,
 -15,   5,  15,  15,  15,  15,   5, -15,
  -5,  15,  25,  25,  25,  25,  15,  -5,
   0,  10,  20,  30,  30,  20,  10,   0,
 -10,  10,  20,  20,  20,  20,  10, -10,
 -20, -10,  10,  10,  10,  10, -10, -20,
 -50, -20, -10, -10, -10, -20, -20, -50,
 -60, -40, -30, -20, -20, -30, -40, -60
};

int Eval::knight_pcsq_eg[64] = {
 -10, -10,  -5,   0,   0,  -5, -10, -10,
  -5,   0,  10,  10,  10,  10,   0,  -5,
   5,  10,  10,  15,  15,  10,  10,   5,
   5,  10,  10,  15,  15,  10,  10,   5,
   0,   0,   5,  10,  10,   5,   0,   0,
  -5,   0,   0,   5,   5,   0,   0,  -5,
 -10, -10,  -5,   0,   0,  -5, -10, -10,
 -20, -15, -10, -10, -10, -10, -15, -20
};

int Eval::bishop_pcsq_mg[64] = {
   0,   5,   0,  -5,  -5,   0,   5,   0,
   0,  10,   5,   5,   5,   5,  10,   0,
  -5,   5,  15,  15,  15,  15,   5,  -5,
  -5,   5,  10,  15,  15,  10,   5,  -5,
 -10,   0,   5,  10,  10,   5,   0, -10,
  -5,   0,   0,   0,   0,   0,   0,  -5,
  -5,  10,   0,   0,   0,   0,  10,  -5,
  -5,  -5, -10, -10, -10, -10,  -5,  -5
};

int Eval::bishop_pcsq_eg[64] = {
   0,   0,  -5,  -5,  -5,  -5,   0,   0,
   0,   5,   0,   0,   0,   0,   5,   0,
  -5,   0,   5,   5,   5,   5,   0,  -5,
  -5,   0,   5,   5,   5,   5,   0,  -5,
  -5,   0,   5,   5,   5,   5,   0,  -5,
  -5,   0,   5,   5,   5,   5,   0,  -5,
   0,   5,   0,   0,   0,   0,   5,   0,
   0,   0,  -5,  -5,  -5,  -5,   0,   0
};

int Eval::king_pcsq_mg[64] = {
  10,  10, -20, -40, -40, -20,  10,  10,
  10,  10, -10, -30, -30, -10,  10,  10,
  10,  10,   0, -20, -20,   0,  10,  10,
  20,  20,  10, -10, -10,  10,  20,  20,
  20,  20,  10, -10, -10,  10,  20,  20,
  30,  30,  20,   0,   0,  20,  30,  30,
  40,  40,  20,   0,   0,  20,  40,  40,
  40,  50,  20,   0,   0,  20,  50,  40
};

int Eval::king_pcsq_eg[64] = {
 -40, -30, -10, -10, -10, -10, -30, -40,
 -20, -10,  10,  10,  10,  10, -10, -20,
 -20,  10,  20,  20,  20,  20,  10, -20,
 -20,  10,  30,  30,  30,  30,  10, -20,
 -25,   5,  15,  25,  25,  15,   5, -25,
 -35,  -5,   5,  15,  15,   5,  -5, -35,
 -45, -15,  -5,   5,   5,  -5, -15, -45,
 -75, -55, -35, -35, -35, -35, -55, -75
};

/*
int Eval::_pcsq_eg[64] = {
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0
};*/

int Eval::pawn_isolated_open_mg = -37;
int Eval::pawn_isolated_mg = -23;
int Eval::pawn_isolated_open_eg = -21;
int Eval::pawn_isolated_eg = -25;
int Eval::pawn_unsupported_open_mg = -26;
int Eval::pawn_unsupported_mg = -4;
int Eval::pawn_unsupported_open_eg = 6;
int Eval::pawn_unsupported_eg = -7;
int Eval::pawn_doubled_mg = -9;
int Eval::pawn_doubled_eg = -3;
int Eval::pawn_advance_mg = 2;
int Eval::pawn_advance_eg = 3;
int Eval::side_to_move_mg = 6;
int Eval::side_to_move_eg = 4;
int Eval::rook_on_open_mg = 25;
int Eval::rook_on_open_eg = 18;
int Eval::rook_on_half_open_mg = 21;
int Eval::rook_on_half_open_eg = -3;
int Eval::bishop_pair_mg = 33;
int Eval::bishop_pair_eg = 58;

int Eval::knight_mob_mg[9] = { -51, -28, -14, 2, 8, 15, 26, 29, 20 };
int Eval::bishop_mob_mg[14] = { -43, -29, -21, -4, 5, 15, 21, 25, 31, 29, 36, 38, 52, 70 };
int Eval::rook_mob_mg[15] = { -13, -14, -17, -15, -12, -7, -5, 0, 5, 13, 11, 11, 18, 10, 26 };
int Eval::rook_mob_eg[15] = { -26, -31, -27, -24, -7, -5, -5, 0, 5, 10, 20, 25, 23, 26, 26 };
int Eval::passed_pawn_mg[8] = { 0, 1, -17, 7, 31, 103, 147, 0 };
int Eval::passed_pawn_eg[8] = { 0, 22, 39, 37, 39, 61, 110, 0 };
