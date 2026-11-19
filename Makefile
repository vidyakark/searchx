CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS  ?= -pthread
BUILD    := build
CORE     := $(BUILD)/tokenizer.o $(BUILD)/index.o $(BUILD)/query.o $(BUILD)/engine.o

all: $(BUILD)/searchx $(BUILD)/gen_corpus $(BUILD)/bench $(BUILD)/run_tests

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/searchx: $(CORE) $(BUILD)/main.o $(BUILD)/server.o
	$(CXX) $^ -o $@ $(LDFLAGS)
$(BUILD)/bench: $(CORE) tools/bench.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
$(BUILD)/gen_corpus: tools/gen_corpus.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@
$(BUILD)/run_tests: $(CORE) tests/test_main.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test: $(BUILD)/run_tests
	./$(BUILD)/run_tests
clean:
	rm -rf $(BUILD)
.PHONY: all test clean
