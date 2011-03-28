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
	Eval(Game* game, PawnStructureTable* pawnt) {
		this->game = game;
		board = game->pos->board;
		this->pawnt = pawnt;
		pawns_array[0] = &board->pawns(0);
		pawns_array[1] = &board->pawns(1);
		king_square[0] = &board->king_square[0];
		king_square[1] = &board->king_square[1];
		buildPcSqTables();
	}

	void newGame() {
	}

	int evaluate() {
		poseval_mg[0] = poseval_eg[0] = poseval[0] = mateval[0] = 0;
		poseval_mg[1] = poseval_eg[1] = poseval[1] = mateval[1] = 0;
		
		attack_points[0] = attack_points[1] = attack_count[0] = attack_count[1] = 0;

		kingmoves[0] = king_attacks[kingSq(0)] | board->king(0);
		kingmoves[1] = king_attacks[kingSq(1)] | board->king(1);
		occupied = board->occupied;
		not_occupied = ~occupied;

		open_files = ~(northFill(southFill(pawns(0))) | northFill(southFill(pawns(1))));
		half_open_files[0] = ~northFill(southFill(pawns(0))) & ~open_files;
		half_open_files[1] = ~northFill(southFill(pawns(1))) & ~open_files;

		// pass 1
		pos = game->pos;
		evalPawnsBothSides();
		for (Side side = 0; side < 2; side++) {
			evalMaterialOneSide(side);
			evalKingOneSide(side);
			evalRooksOneSide(side);
			evalKnightsOneSide(side);
			evalBishopsOneSide(side);
			evalQueensOneSide(side);
		}
		// pass 2 
		for (Side side = 0; side < 2; side++) {
			evalPassedPawnsOneSide(side);
			if (pawn_attacks[side] & kingmoves[side ^ 1]) {
				attack_count[side]++;
			}
			poseval_mg[side] += 2*attack_points[side]*max(0, attack_count[side] - 1);
		}

		double stage = (pos->material.value()-pos->material.pawnValue())/(double)pos->material.max_value;

		int pos_eval = ((int)((poseval_mg[0]-poseval_mg[1])*stage))
						+ ((int)((poseval_eg[0]-poseval_eg[1])*(1-stage)))
						+ (poseval[0]-poseval[1]);

		int mat_eval = mateval[0] - mateval[1];

		int eval = pos_eval + mat_eval;

		return pos->material.evaluate(pos->side_to_move == 1 ? -eval : eval, pos->side_to_move, board);
	}

	__forceinline const BB& attacks(Side side) {
		return all_attacks[side];
	}

protected:
	__forceinline void evalPawnsBothSides() {
		all_attacks[0] = pawn_attacks[0] = pawnEastAttacks[0](pawns(0)) | pawnWestAttacks[0](pawns(0));
		all_attacks[1] = pawn_attacks[1] = pawnEastAttacks[1](pawns(1)) | pawnWestAttacks[1](pawns(1));

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

	__forceinline void evalPawnsOneSide(const int c) {
		for (BB bb = pawns(c); bb; ) {
			Square sq = lsb(bb);

			pawn_eval_mg[c] += pawn_pcsq[flip[c][sq]];
			pawn_eval_eg[c] += pawn_pcsq[flip[c][sq]];

			if (board->isPawnPassed(sq, c)) {
				passed_pawn_files[c] |= 1 << file(sq);
			}
			bool open_file = !board->isPieceOnFile(Pawn, sq, c ^ 1);
			if (board->isPawnIsolated(sq, c)) {
				pawn_eval_mg[c] += open_file ? -40 : -20; 
				pawn_eval_eg[c] += -20;
			}
			else if (board->isPawnBehind(sq, c)) {
				pawn_eval_mg[c] += open_file ? -20 : -8;
				pawn_eval_eg[c] += -8;
			}
			resetLSB(bb);
			if (bbFile(sq) & bb) {
				pawn_eval_mg[c] += -15; 
				pawn_eval_eg[c] += -15;
			}
		}
	}

	__forceinline void evalKnightsOneSide(const int c) {
		for (BB knights = board->knights(c); knights; resetLSB(knights)) {
			Square sq = lsb(knights);
			poseval[c] += knight_pcsq[flip[c][sq]];
			const BB& attacks = knight_attacks[sq];
			int x = popCount(attacks & not_occupied & ~pawn_attacks[c ^ 1]);

			poseval[c] += knight_mobility_mg[x];
			all_attacks[c] |= attacks;
			bool outpost = (passed_pawn_front_span[c][sq] & (pawns(c ^  1) & ~pawn_front_span[c][sq])) == 0;

			if (outpost && (pawn_attacks[c] & bbSquare(sq))) {
				int d = chebyshev_distance[sq][kingSq(c ^ 1)];
				poseval[c] += 5*(7-d);
				poseval_eg[c] += 2*(7-d);
			}
			int y = popCount(attacks & kingmoves[c ^ 1])*4;
			attack_points[c] += y;
			attack_count[c] += y ? 1 : 0;
		}
	}

	__forceinline void evalBishopsOneSide(const int c) {
		for (BB bishops = board->bishops(c); bishops; resetLSB(bishops)) {
			Square sq = lsb(bishops);
			const BB& bbsq = bbSquare(sq);

			poseval[c] += bishop_pcsq[flip[c][sq]];
			const BB attacks = Bmagic(sq, occupied);
			int x = popCount(attacks & not_occupied);
			poseval[c] += bishop_mobility_mg[x];
			all_attacks[c] |= attacks;
			if (bishop_trapped_a7h7[c] & bbsq) {
				int x = file(sq)/7;
				if ((pawns(c ^  1) & pawns_trap_bishop_a7h7[x][c]) == pawns_trap_bishop_a7h7[x][c]) {
					poseval[c] -= 110;
				}
			}
			bool outpost = (passed_pawn_front_span[c][sq] & (pawns(c ^  1) & ~pawn_front_span[c][sq])) == 0;
			if (outpost && (pawn_attacks[c] & bbsq)) {
				int d = chebyshev_distance[sq][kingSq(c ^ 1)];
				poseval[c] += 5*(7-d);
				poseval_eg[c] += 2*(7-d);
			}
			const BB attacks2 = Bmagic(sq, occupied & ~board->queens(c) & ~board->rooks(c ^ 1) & 
				~board->queens(c ^ 1));

			int y = popCount(attacks2 & kingmoves[c ^ 1])*4;
			attack_points[c] += y;
			attack_count[c] += y ? 1 : 0;
		}
	}

	__forceinline void evalRooksOneSide(const int c) {
		for (BB rooks = board->rooks(c); rooks; resetLSB(rooks)) {
			Square sq = lsb(rooks);
			const BB& bbsq = bbSquare(sq); 
			if (bbsq & open_files) { 
				poseval[c] += 20;
			}
			else if (bbsq & half_open_files[c]) { 
				poseval[c] += 10;
			}
			if ((bbsq & rank_7[c]) && (rank_7_and_8[c] & (pawns(c ^  1) | board->king(c ^ 1)))) {
				poseval[c] += 20;
			}
			const BB attacks = Rmagic(sq, occupied);
			int x = popCount(attacks & not_occupied);
			poseval[c] += rook_mobility_mg[x];
			all_attacks[c] |= attacks;

			const BB attacks2 = Rmagic(sq, occupied & ~board->rooks(c) & ~board->queens(c) & ~board->queens(c ^ 1));
			int y = popCount(attacks2 & kingmoves[c ^ 1])*6;
			attack_points[c] += y;
			attack_count[c] += y ? 1 : 0;
		}
	}

	__forceinline void evalQueensOneSide(const int c) {
		for (BB queens = board->queens(c); queens; resetLSB(queens)) {
			Square sq = lsb(queens);
			const BB& bbsq = bbSquare(sq);
			poseval_mg[c] += queen_mg_pcsq[flip[c][sq]];
			if ((bbsq & rank_7[c]) && (rank_7_and_8[c] & (pawns(c ^  1) | board->king(c ^ 1)))) {
				poseval[c] += 20;
			}
			const BB attacks = Qmagic(sq, occupied);
			all_attacks[c] |= attacks;

			int d = chebyshev_distance[sq][kingSq(c ^ 1)];
			poseval[c] += 5*(7-d);
			poseval_eg[c] += 2*(7-d);

			const BB attacks2 = Bmagic(sq, occupied & ~board->bishops(c) & ~board->queens(c)) | 
				Rmagic(sq, occupied & ~board->rooks(c) & ~board->queens(c));

			int y = popCount(attacks2 & kingmoves[c ^ 1])*12;
			attack_points[c] += y;
			attack_count[c] += y ? 1 : 0;
		}
	}

	__forceinline void evalMaterialOneSide(const int c) {
		mateval[c] += pos->material.material_value[c];
		if (pos->material.count(c, Bishop) == 2) {
			poseval[c] += max(30, 64 - (pos->material.pawnCount()*4));
		}
	}

	__forceinline void evalKingOneSide(const int c) {
		Square sq = lsb(board->king(c));
		const BB& bbsq = bbSquare(sq);

		poseval_mg[c] += king_mg_pcsq[flip[c][sq]];
		poseval_eg[c] += king_eg_pcsq[flip[c][sq]];

		int pawn_shield = -45 + 15*popCount((pawnPush[c](bbsq) | pawnWestAttacks[c](bbsq) | 
			pawnEastAttacks[c](bbsq)) & pawns(c));

		poseval_mg[c] += pawn_shield; 

		if (board->queens(c ^ 1) || popCount(board->rooks(c ^ 1)) > 1) {
			BB eastwest = bbsq | westOne(bbsq) | eastOne(bbsq);
			poseval_mg[c] += -15*popCount(open_files & eastwest);
			poseval_mg[c] += -10*popCount(half_open_files[c] & eastwest);
		}

		if (((c == 0) && 
				(((sq == f1 || sq == g1) && (bbSquare(h1) & board->rooks(0))) || 
				((sq == c1 || sq == b1) && (bbSquare(a1) & board->rooks(0))))) ||
			((c == 1) && 
				(((sq == f8 || sq == g8) && (bbSquare(h8) & board->rooks(1))) || 
				((sq == c8 || sq == b8) && (bbSquare(a8) & board->rooks(1))))))
		{
			poseval_mg[c] += -180;
		}
		all_attacks[c] |= king_attacks[kingSq(c)];
	}

	__forceinline void evalPassedPawnsOneSide(int c) {
		for (BB files = pawnp ? pawnp->passed_pawn_files[c] : 0; files; resetLSB(files)) {
			for (BB bb = bbFile(lsb(files)) & pawns(c); bb; resetLSB(bb)) {
				int sq = lsb(bb);

				const BB& front_span = pawn_front_span[c][sq];
				int r = c == 0 ? rank(sq) : 7 - rank(sq);

				int score = r*20; 
				poseval_mg[c] += score;

				score = r*14; 
				score += 3*r*(front_span & board->occupied_by_side[c] ? 0 : 1);
				score += 2*r*(front_span & board->occupied_by_side[c ^ 1] ? 0 : 1);
				score += 3*r*(front_span & all_attacks[c ^ 1] ? 0 : 1);

				int d_us = 7 - chebyshev_distance[sq][kingSq(c)];
				int d_them = 7 - chebyshev_distance[sq][kingSq(c ^ 1)];
				score += r*(d_us*2-d_them*2);
				poseval_eg[c] += score;
			}
		}
	}

	__forceinline const BB& pawns(Side side) {
		return *pawns_array[side];
	}

	__forceinline Square kingSq(Side side) {
		return *king_square[side];
	}

	void buildPcSqTables() {
		for (int sq = a1; sq <= h8; sq++) {
			int r = rank(sq); 
			int f = file(sq);
			knight_pcsq[flip[0][sq]] = min(minor_file_value[f], minor_rank_value[r]); 
			bishop_pcsq[flip[0][sq]] = min(minor_file_value[f], minor_rank_value[r]); 
			pawn_pcsq[flip[0][sq]] = (r > 1 && r < 7) ? pawn_file_value[f] + pawn_file_value_inc[f]*(r-2) : 0; 
			king_mg_pcsq[flip[0][sq]] = (r > 0 || f == 3 || f == 5) ? -40 : 0;
			king_eg_pcsq[flip[0][sq]] = king_file_value_eg[f] + king_rank_value_eg[r]; 
			queen_mg_pcsq[flip[0][sq]] = (r == 0 ? -10 : -20) + queen_file_value_mg[f];
		}
		pawn_pcsq[flip[0][d2]] = -40; 
		pawn_pcsq[flip[0][e2]] = -40; 
		pawn_pcsq[flip[0][d3]] = -10; 
		pawn_pcsq[flip[0][e3]] = -10; 
		king_mg_pcsq[flip[0][b1]] = 20;
		king_mg_pcsq[flip[0][c1]] = 40;
		king_mg_pcsq[flip[0][g1]] = 40;
		king_mg_pcsq[flip[0][h1]] = 20;
		knight_pcsq[flip[0][a8]] = -25; 
		knight_pcsq[flip[0][h8]] = -25; 
		knight_pcsq[flip[0][b8]] = -20; 
		knight_pcsq[flip[0][g8]] = -20; 
		queen_mg_pcsq[flip[0][d1]] = 0;
		//print_tables();
	}

	void print_tables() {
		print_pcsq_tables("pawn:\n\n", pawn_pcsq);
		print_pcsq_tables("\n\nknight:\n\n", knight_pcsq);
		print_pcsq_tables("\n\nbishop:\n\n", bishop_pcsq);
		print_pcsq_tables("\n\nqueen mg:\n\n", queen_mg_pcsq);
		print_pcsq_tables("\n\nking mg:\n\n", king_mg_pcsq);
		print_pcsq_tables("\n\nking eg:\n\n", king_eg_pcsq);
	}

	void print_pcsq_tables(const char* table, int* values) {
		cout << table;
		for (int i = 0; i <= 63; i++) {
			if (i % 8 == 0) cout << endl;
			printf("%4d  ",  values[i]);
		}
	}

	Board* board;
	Position* pos;
	Game* game;
	PawnStructureTable* pawnt;
	PawnEntry* pawnp;

	const BB* pawns_array[2];
	const Square* king_square[2];

	BB open_files, half_open_files[2];
	int passed_pawn_files[2], attack_points[2], attack_count[2];
	BB pawn_attacks[2], all_attacks[2], kingmoves[2], occupied, not_occupied;

	int pawn_pcsq[64], knight_pcsq[64], bishop_pcsq[64], queen_mg_pcsq[64], king_mg_pcsq[64], king_eg_pcsq[64];
	int poseval_mg[2], poseval_eg[2], poseval[2], mateval[2], pawn_eval_mg[2], pawn_eval_eg[2];

	static int minor_file_value[8], minor_rank_value[8], pawn_file_value[8], pawn_file_value_inc[8], king_file_value_eg[8], 
		king_rank_value_eg[8], bishop_mobility_mg[14], rook_mobility_mg[15], knight_mobility_mg[9], queen_file_value_mg[8];

	static BB bishop_trapped_a7h7[2], pawns_trap_bishop_a7h7[2][2];
};

BB Eval::bishop_trapped_a7h7[2] = { 
	bbSquare(a7) | bbSquare(h7), bbSquare(a2) | bbSquare(h2) 
};
BB Eval::pawns_trap_bishop_a7h7[2][2] = { 
	{ bbSquare(b6) | bbSquare(c7), bbSquare(b3) | bbSquare(c2) }, 
	{ bbSquare(g6) | bbSquare(f7), bbSquare(g3) | bbSquare(f2) } 
};
int Eval::bishop_mobility_mg[14] = { 
	-15, -5, 0, 6, 12, 16, 20, 22, 22, 24, 24, 24, 26, 26 
};
int Eval::rook_mobility_mg[15] = { 
	-15, -7, -5, 0, 5, 7, 10, 14, 15, 19, 20, 21, 25, 26, 27 
};
int Eval::knight_mobility_mg[9] = { 
	-15, -8, -4, 0, 3, 6, 9, 12, 15 
};
int Eval::minor_file_value[8] = { 
	-15, -5, 0, 10, 10, 0, -5, -15 
};
int Eval::minor_rank_value[8] = { 
	-20, -5, 0, 10, 10, 0, -5, -15 
};
int Eval::pawn_file_value[8] = { 
	3, 4, 5, 6, 6, 5, 4, 3 
};
int Eval::pawn_file_value_inc[8] = { 
	1, 2, 3, 4, 4, 3, 2, 1 
};
int Eval::king_file_value_eg[8] = { 
	0, 8, 16, 24, 24, 16, 8, 0 
};
int Eval::king_rank_value_eg[8] = { 
	0, 8, 16, 24, 24, 16, 8, 0 
};
int Eval::queen_file_value_mg[8] = { 
	-20, -10, 0, 0, 0, 0, -10, -20 
};

