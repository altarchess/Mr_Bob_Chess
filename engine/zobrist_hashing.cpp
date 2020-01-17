#include "zobrist_hashing.h"
#include <stdlib.h>



Zobrist::Zobrist() {

  for (uint8_t i = 0; i < 64; i++) {
    for (uint8_t j = 0; j < 12; j++) {
      table[i][j] = (uint64_t)((rand() & 0xffff) | ((rand() & 0xffff) << 16) | (((uint64_t)rand() & 0xffff) << 32) | (((uint64_t)rand() & 0xffff) << 48));
    }
  }

}

uint64_t Zobrist::hashBoard(std::unordered_map<uint8_t, uint64_t>& pieces, uint64_t& occupied, uint64_t& blacks, bool turn) {
  uint64_t ret = turn;
  for (uint8_t i = 0; i < 64; i++) {
    uint64_t shiftI = (1ULL << i);
    
    if ((occupied & shiftI) != 0) {
      uint8_t j = 0;

      if ((blacks & shiftI) != 0) {
        j += 6;
      }

      if ((pieces[0] & shiftI) != 0) {
        j += 0;
      }
      else if ((pieces[1] & shiftI) != 0) {
        j += 1;
      }
      else if ((pieces[2] & shiftI) != 0) {
        j += 2;
      }
      else if ((pieces[3] & shiftI) != 0) {
        j += 3;
      }
      else if ((pieces[4] & shiftI) != 0) {
        j += 4;
      }
      else if ((pieces[5] & shiftI) != 0) {
        j += 5;
      }

      ret ^= table[i][j];
    }
  }

  return ret;
}
