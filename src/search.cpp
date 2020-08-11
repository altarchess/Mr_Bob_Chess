#include "search.h"



uint64_t nodes;
double totalTime;
int seldepth;
int height;
std::atomic<bool> exit_thread_flag;
extern int pieceValues[6];


int lmrReduction[64][64];
void InitLateMoveArray() {
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            lmrReduction[i][j] = (exp(2) * log(i) * log(j) + 12) / (exp(exp(1)));
        }
    }
}



int qsearch(Bitboard &b, int depth, int alpha, int beta, int height) {

    #ifdef DEBUGHASH
    b.debugZobristHash();
    #endif

    nodes++;
    seldepth = std::min(depth, seldepth);
    if (exit_thread_flag) {
        return 0;
    }

    if (b.isDraw()) {
        return 0;
    }

    bool inCheck = b.InCheck();
    int stand_pat = inCheck? -MATE_VALUE + height : 0;
    if (!inCheck) {
        stand_pat = std::max(alpha, b.evaluate());
        if (stand_pat >= beta) {
            return stand_pat;
        }

        if (alpha < stand_pat) {
            alpha = stand_pat;
        }

        if (stand_pat < alpha - MGVAL(pieceValues[4])) {
            return stand_pat;
        }
    }

    MOVE move;
    MoveList moveList;
    int ret = -INFINITY_VAL;
    int numMoves = 0;

    inCheck? b.generate(moveList, 0, NO_MOVE) : b.generate_captures_promotions(moveList, NO_MOVE);
    while (moveList.get_next_move(move)) {

        if (!inCheck && (move & PROMOTION_FLAG) == 0 && b.seeCapture(move) < 0) {
            continue;
        }

        if (!b.isLegal(move)) {
            continue;
        }

        b.make_move(move);
        numMoves++;


        int score = -qsearch(b, depth - 1, -beta, -alpha, height + 1);
        b.undo_move(move);
        if (score > ret) {
            ret = score;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) {
                    return score;
                }
            }
        }

    }


    return numMoves == 0? stand_pat : ret;

}





int pvSearch(Bitboard &b, int depth, int alpha, int beta, bool canNullMove, int height) {

    #ifdef DEBUGHASH
    b.debugZobristHash();
    #endif

    if (exit_thread_flag) {
        return 0;
    }

    if (b.isDraw()) {
        return 0;
    }

    if (alpha < 0 && b.isRepetition()) {
        if (beta <= 0) {
            return 0;
        }
    }

    if (beta > 0 && b.noPotentialWin()) {
        if (alpha >= 0) {
            return 0;
        }
    }

    if (depth <= 0) {
        return qsearch(b, depth - 1, alpha, beta, height);
    }

    nodes++;

    MOVE move;
    MOVE bestMove = NO_MOVE;
    MoveList moveList;

    int ret = -INFINITY_VAL;
    int numMoves = 0;
    int prevAlpha = alpha;
    bool isPv = alpha == beta - 1? false : true;


    // Transposition table for duplicate detection:
    // Get the hash key
    ZobristVal hashedBoard;
    uint64_t posKey = b.getPosKey();
    bool ttRet = false;
    bool hashed = b.probeTT(posKey, hashedBoard, depth, ttRet, alpha, beta);

    if (ttRet) {
        return hashedBoard.score;
    }


    int eval = hashed? hashedBoard.score : b.evaluate();
    evalStack[height] = eval;
    bool improving = height >= 2? eval > evalStack[height - 2] : false;
    b.removeKiller(height + 1);
    bool isCheck = b.InCheck();

    // Razoring
    if (!isPv && !isCheck && depth <= 1 && eval <= alpha - 350) {
        return qsearch(b, -1, alpha, beta, height);
    }

    if (!isPv && !isCheck && depth <= 6 && eval - 220 * depth >= beta && eval < 9000) {
        return eval;
    }


    if (!isPv && canNullMove && !isCheck && eval >= beta && depth >= 2 && b.nullMoveable()) {
        int R = 3 + depth / 8;
        b.make_move(NULL_MOVE);
        int nullRet = -pvSearch(b, depth - R - 1, -beta, -beta + 1, false, height + 1);
        b.undo_move(NULL_MOVE);

        if (nullRet >= beta && nullRet < 9000) {
            return nullRet;
        }
    }

    int mateDistance = MATE_VALUE - height;
    if (mateDistance < beta) {
        beta = mateDistance;
        if (alpha >= mateDistance) {
            return mateDistance;
        }
    }
    mateDistance = -MATE_VALUE + height;
    if (mateDistance > alpha) {
        alpha = mateDistance;
        if (beta <= mateDistance) {
            return mateDistance;
        }
    }


    int quietsSearched = 0;
    MOVE quiets[MAX_NUM_MOVES];
    b.generate(moveList, height, hashedBoard.move);
    while (moveList.get_next_move(move)) {
        int score;
        int extension = 0;
        bool giveCheck = b.InCheck();
        bool isQuiet = (move & (CAPTURE_FLAG | PROMOTION_FLAG)) == 0;


        if (giveCheck) {
            extension = 1;
        }

        if (!isPv && !isCheck) {
            if (isQuiet && !giveCheck) {
                if (depth <= 6 && numMoves > 0 && eval + 215 * depth <= alpha && alpha < 9000) {
                    continue;
                }

                if (depth == 1 && quietsSearched > (improving? 7 : 5)) {
                    continue;
                }
                else if (depth == 2 && quietsSearched > (improving? 10 : 8)) {
                    continue;
                }
            }

            if (depth <= 3 && numMoves > 0 && (move & PROMOTION_FLAG) == 0) {
                if (depth == 1 && b.seeCapture(move) < 0) {
                    continue;
                }
                else if (depth == 2 && b.seeCapture(move) < -350) {
                    continue;
                }
                else if (depth == 3 && b.seeCapture(move) < -500) {
                    continue;
                }
            }
        }

        if (!b.isLegal(move)) {
            continue;
        }

        b.make_move(move);
        int newDepth = depth + extension;
        if (numMoves == 0) {
            score = -pvSearch(b, newDepth - 1, -beta, -alpha, true, height + 1);
        }
        else if (depth >= 3 && isQuiet && !isCheck && !giveCheck && !extension) {
            int lmr = lmrReduction[std::min(63, numMoves)][std::min(63, depth)];

            lmr -= b.isKiller(height, move);
            lmr += !improving && numMoves >= 6;
            lmr -= 2 * isPv;
            // lmr += see;

            lmr = std::min(depth - 1, std::max(lmr, 0));
            score = -pvSearch(b, newDepth - 1 - lmr, -alpha - 1, -alpha, true, height + 1);
            if (score > alpha) {
                score = -pvSearch(b, newDepth - 1, -alpha - 1, -alpha, true, height + 1);
                if (score > alpha && score < beta) {
                    score = -pvSearch(b, newDepth - 1, -beta, -alpha, true, height + 1);
                }
            }
        }
        else {
            score = -pvSearch(b, newDepth - 1, -alpha - 1, -alpha, true, height + 1);
            if (score > alpha && score < beta) {
                score = -pvSearch(b, newDepth - 1, -beta, -alpha, true, height + 1);
            }
        }
        b.undo_move(move);

        if (exit_thread_flag) {
            return 0;
        }

        numMoves++;

        if (score > ret) {
            ret = score;
            bestMove = move;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) {
                    break;
                }
            }
        }

        if (isQuiet) {
            quiets[quietsSearched] = move;
            quietsSearched++;
        }

    }


    if (alpha >= beta && ((bestMove & (CAPTURE_FLAG | PROMOTION_FLAG)) == 0)) {
        b.insertKiller(height, move);
        b.insertCounterMove(move);

        int hist = history[b.getSideToMove()][get_move_from(bestMove)][get_move_to(bestMove)] * std::min(depth, 20) / 23;
        history[b.getSideToMove()][get_move_from(bestMove)][get_move_to(bestMove)] += 32 * (depth * depth) - hist;

        for (int i = 0; i < quietsSearched; i++) {
            int hist2 = history[b.getSideToMove()][get_move_from(quiets[i])][get_move_to(quiets[i])] * std::min(depth, 20) / 23;
            history[b.getSideToMove()][get_move_from(quiets[i])][get_move_to(quiets[i])] += 30 * (-depth * depth) - hist2;
        }
    }



    if (numMoves == 0) {
        if (isCheck) {
            return -MATE_VALUE + height;
        }
        else {
            return 0;
        }
    }

    if (exit_thread_flag) {
        return 0;
    }

    assert(alpha >= prevAlpha);
    if (alpha >= beta) {
        assert(move != 0);
        b.saveTT(bestMove, ret, depth, 1, posKey);
    }
    else if (prevAlpha >= ret) {
        assert (bestMove != 0);
        b.saveTT(bestMove, ret, depth, 2, posKey);
    }
    else {
        assert (bestMove != 0);
        b.saveTT(bestMove, ret, depth, 0, posKey);
    }

    return ret;

}



BestMoveInfo pvSearchRoot(Bitboard &b, int depth, MoveList moveList, int alpha, int beta) {

    nodes++;
    MOVE move;
    MOVE bestMove = 0;
    int numMoves = 0;
    int ret = -INFINITY_VAL;
    int height = 0;

    // Transposition table for duplicate detection:
    ZobristVal hashedBoard;
    uint64_t posKey = b.getPosKey();
    bool ttRet = false;
    bool hashed = b.probeTT(posKey, hashedBoard, depth, ttRet, alpha, beta);

    int eval = hashed? hashedBoard.score : b.evaluate();
    evalStack[height] = eval;
    while (moveList.get_next_move(move)) {

        int tempRet;

        if (!b.isLegal(move)) {
            continue;
        }

        b.make_move(move);
        if (totalTime > 3000) {
            std::cout << "info depth " << depth << " currmove " << TO_ALG[get_move_from(move)] + TO_ALG[get_move_to(move)] << " currmovenumber "<< numMoves + 1 << std::endl;
        }

        if (numMoves == 0) {
            tempRet = -pvSearch(b, depth - 1, -beta, -alpha, true, height + 1);
        }
        else if (depth >= 3 && (move & CAPTURE_FLAG) == 0 && (move & PROMOTION_FLAG) == 0) {
            int lmr = lmrReduction[std::min(63, numMoves)][std::min(63, depth)];

            lmr = std::min(depth - 1, std::max(lmr, 0));
            tempRet = -pvSearch(b, depth - 1 - lmr, -alpha - 1, -alpha, true, height + 1);
            if (tempRet > alpha) {
                tempRet = -pvSearch(b, depth - 1, -alpha - 1, -alpha, true, height + 1);
                if (tempRet > alpha && tempRet < beta) {
                    tempRet = -pvSearch(b, depth - 1, -beta, -alpha, true, height + 1);
                }
            }
        }
        else {
            tempRet = -pvSearch(b, depth - 1, -alpha - 1, -alpha, true, height + 1);
            if (tempRet > alpha && tempRet < beta) {
                tempRet = -pvSearch(b, depth - 1, -beta, -alpha, true, height + 1);
            }
        }
        b.undo_move(move);

        numMoves++;

        if (exit_thread_flag) {
            break;
        }

        if (tempRet > ret) {
            ret = tempRet;
            bestMove = move;
            if (tempRet > alpha) {
                if (tempRet >= beta) {
                    break;
                }
                alpha = tempRet;
            }
        }

    }

    if (numMoves == 0) {
        exit_thread_flag = true;
    }


    if (!exit_thread_flag) {
        assert (bestMove != 0);
        b.saveTT(bestMove, ret, depth, 0, posKey);
    }

    return BestMoveInfo(bestMove, ret);

}



void search(Bitboard &b, int depth) {

    MOVE bestMove;
    MoveList moveList;
    std::string algMove;
    uint64_t nps;
    uint64_t nodesTotal = 0;
    totalTime = 0;

    ZobristVal hashedBoard;
    uint64_t posKey = b.getPosKey();
    bool ttRet = false;

    int alpha;
    int beta;
    int tempAlpha;
    int tempBeta;
    int score = 0;

    b.clearHashStats();

    b.generate(moveList, 0, NO_MOVE);
    for (int i = 1; i <= depth; i++) {

        int delta = ASPIRATION_DELTA;
        int aspNum = 0;
        nodes = 0;
        seldepth = i;

        // Use aspiration window with depth >= 4
        if (i >= 4) {
            alpha = hashedBoard.score - delta;
            beta = hashedBoard.score + delta;
        }
        else {
            alpha = -INFINITY_VAL;
            beta = INFINITY_VAL;
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        while (true) {

            pvSearchRoot(b, i, moveList, alpha, beta);
            bool hashed = b.probeTT(posKey, hashedBoard, i, ttRet, tempAlpha, tempBeta);

            if (i > 1 && !exit_thread_flag) {
                assert(hashed);
                (void) hashed;
            }

            moveList.set_score_move(hashedBoard.move, 1400000 + (i * 100) + aspNum);

            if (exit_thread_flag) {
                break;
            }

            // Update the aspiration score
            // Fail high
            if (hashedBoard.score >= beta) {
                beta = hashedBoard.score + delta;
            }
            // Fail low
            else if (hashedBoard.score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = hashedBoard.score - delta;
            }
            // exact
            else {
                break;
            }

            aspNum++;
            delta += delta / 3 + 3;

        }
        auto t2 = std::chrono::high_resolution_clock::now();

        if (exit_thread_flag) {
            break;
        }

        nodesTotal += nodes;
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds> (t2 - t1).count();
        totalTime += (double) diff;

        if ((double) diff == 0) {
            nps = 0;
        }
        else {
            nps = (uint64_t) ((double) nodesTotal * 1000.0) / ((double) totalTime);
        }

        bestMove = hashedBoard.move;
        algMove = TO_ALG[get_move_from(bestMove)] + TO_ALG[get_move_to(bestMove)];

        switch (bestMove & MOVE_FLAGS) {
            case QUEEN_PROMOTION_FLAG:
                algMove += "q";
                break;
            case QUEEN_PROMOTION_CAPTURE_FLAG:
                algMove += "q";
                break;
            case ROOK_PROMOTION_FLAG:
                algMove += "r";
                break;
            case ROOK_PROMOTION_CAPTURE_FLAG:
                algMove += "r";
                break;
            case BISHOP_PROMOTION_FLAG:
                algMove += "b";
                break;
            case BISHOP_PROMOTION_CAPTURE_FLAG:
                algMove += "b";
                break;
            case KNIGHT_PROMOTION_FLAG:
                algMove += "n";
                break;
            case KNIGHT_PROMOTION_CAPTURE_FLAG:
                algMove += "n";
                break;
        }

        std::string cpScore = " score cp ";
        score = hashedBoard.score;
        if (std::abs(hashedBoard.score) >= MATE_VALUE - 500) {
            score = (MATE_VALUE - std::abs(hashedBoard.score) + 1) / 2;
            if (hashedBoard.score < 0) {
                score = -score;
            }
            cpScore = " score mate ";
        }

        std::cout << "info depth " << i << " seldepth " << std::abs(seldepth) - 1 + i << cpScore << score <<
            " nodes " << nodesTotal << " nps " << nps << " hashfull " << b.getHashFull() << " time " << (int) totalTime << " pv" << b.getPv() << std::endl;

    }

    std::cout << "bestmove " << algMove << std::endl;

}
