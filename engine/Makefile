NAME =  chess-engine
TEST_NAME = chess-engine-tests
TUNE_NAME = chess-engine-tune

SRCS = $(wildcard src/*.cpp src/app/*.cpp src/eval/*.cpp src/search/*.cpp src/move/*.cpp)
ENGINE_SRCS = src/Board.cpp src/openingBook.cpp src/printers.cpp $(wildcard src/app/*.cpp src/eval/*.cpp src/search/*.cpp src/move/*.cpp)
TUNING_MODEL_SRCS = src/tuning/tuning_validation.cpp
TEST_SRCS = $(ENGINE_SRCS) $(TUNING_MODEL_SRCS) tests.cpp
TUNE_SRCS = $(ENGINE_SRCS) src/tuning/eval_param_registry.cpp src/tuning/texel_main.cpp

OBJS = $(SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
TUNE_OBJS = $(TUNE_SRCS:.cpp=.o)

CXX ?= g++

BASE_CXXFLAGS := -Wall -Wextra -I./include -std=c++2b
OPT_FLAGS ?= -O3
ARCH_FLAGS ?= -march=native
RELEASE_FLAGS ?= -flto -DNDEBUG -funroll-loops
TEST_OPT_FLAGS ?=
TUNE_OPT_FLAGS ?= -O2
TUNE_DEFS := -DEVAL_TUNING_MODE -DNDEBUG
LDFLAGS ?=
LDLIBS ?= -lpthread

CXXFLAGS = $(BASE_CXXFLAGS) $(OPT_FLAGS) $(ARCH_FLAGS) $(RELEASE_FLAGS)
TEST_CXXFLAGS = $(BASE_CXXFLAGS) $(TEST_OPT_FLAGS) $(ARCH_FLAGS)
TUNE_CXXFLAGS = $(BASE_CXXFLAGS) $(TUNE_OPT_FLAGS) $(ARCH_FLAGS) $(TUNE_DEFS)

RM = rm -rf

all : $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean :
	@$(RM) $(OBJS) $(TEST_OBJS) $(TUNE_OBJS)

fclean : clean
	@$(RM) $(NAME) $(TEST_NAME) $(TUNE_NAME)

re : fclean all

test: $(TEST_NAME)
	./$(TEST_NAME)

$(TEST_NAME): $(TEST_OBJS)
	$(CXX) $(TEST_CXXFLAGS) $(LDFLAGS) $(TEST_OBJS) $(LDLIBS) -o $(TEST_NAME)

tune: $(TUNE_NAME)

$(TUNE_NAME): $(TUNE_OBJS)
	$(CXX) $(TUNE_CXXFLAGS) $(LDFLAGS) $(TUNE_OBJS) $(LDLIBS) -o $(TUNE_NAME)

print-config:
	@echo "CXX=$(CXX)"
	@echo "CXXFLAGS=$(CXXFLAGS)"
	@echo "TEST_CXXFLAGS=$(TEST_CXXFLAGS)"
	@echo "TUNE_CXXFLAGS=$(TUNE_CXXFLAGS)"
	@echo "LDFLAGS=$(LDFLAGS)"
	@echo "LDLIBS=$(LDLIBS)"

.PHONY: all clean fclean re test tune print-config
