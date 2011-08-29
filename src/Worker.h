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

class Worker {
public:
	void start(Config* config,  Game* master, TTable* transt, PSTable* pawnt) {
		game = new Game(config);
		game->copy(master);
		see = new SEE(game);
		eval = new Eval(game, pawnt, see);
		search = new Search(game, eval, see, transt);
		thread = new Thread(search);
		thread->start();
	}

	void stop() {
		search->stop();
		while (!search->isStopped()) {
			Sleep(1);
		}
		delete thread;
		delete search;
		delete eval;
		delete see;
		delete game;
	}

private:
	Game* game;
	Eval* eval;
	SEE* see;
	Search* search;
	Thread* thread;
};
