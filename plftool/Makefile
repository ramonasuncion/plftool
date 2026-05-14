CXXFLAGS = -Isrc -std=c++20 -O2 \
           -Wall -Wextra -Wpedantic \
           -Wformat=2 -Wno-unused-parameter -Wshadow \
           -Wwrite-strings -Wmissing-include-dirs
           # -g -DDEBUG

SRC = src
BIN = bin
DOCS = docs

SRCS_CPP = $(wildcard $(SRC)/*.cpp)
OBJS_CPP = $(SRCS_CPP:$(SRC)/%.cpp=$(BIN)/%.o)

main: $(OBJS_CPP) | mkbin
	$(CXX) $(CXXFLAGS) $^ -o $(BIN)/plftool -lz

$(BIN)/%.o: $(SRC)/%.cpp | mkbin
	$(CXX) $(CXXFLAGS) -c $< -o $@

mkbin:
	@ mkdir -p $(BIN)

clean:
	@ rm -rf $(BIN)/* core* *~ $(SRC)/*~ $(DOCS)/* *.dSYM

.PHONY: main mkbin doc clean
