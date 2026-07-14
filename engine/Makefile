NAME =  chess-engine
TEST_NAME = chess-engine-tests

SRCS = $(wildcard src/*.cpp src/app/*.cpp src/eval/*.cpp src/search/*.cpp src/move/*.cpp src/time/*.cpp)
ENGINE_SRCS = src/Board.cpp src/openingBook.cpp src/printers.cpp $(wildcard src/app/*.cpp src/eval/*.cpp src/search/*.cpp src/move/*.cpp src/time/*.cpp)
TUNING_MODEL_SRCS = src/tuning/tuning_validation.cpp
TEST_SRCS = $(ENGINE_SRCS) $(TUNING_MODEL_SRCS) tests.cpp

OBJS = $(SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

CXX ?= g++

BASE_CXXFLAGS := -Wall -Wextra -I./include -std=c++2b
OPT_FLAGS ?= -O3
ARCH_FLAGS ?= -march=native
RELEASE_FLAGS ?= -flto -DNDEBUG -funroll-loops
TEST_OPT_FLAGS ?=
LDFLAGS ?=
LDLIBS ?= -lpthread

CXXFLAGS = $(BASE_CXXFLAGS) $(OPT_FLAGS) $(ARCH_FLAGS) $(RELEASE_FLAGS)
TEST_CXXFLAGS = $(BASE_CXXFLAGS) $(TEST_OPT_FLAGS) $(ARCH_FLAGS)

RM = rm -rf

all : $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean :
	@$(RM) $(OBJS) $(TEST_OBJS)

fclean : clean
	@$(RM) $(NAME) $(TEST_NAME)

re : fclean all

test: $(TEST_NAME) $(NAME)
	./$(TEST_NAME)
	ENGINE=./$(NAME) ./scripts/test_uci_profile_identity.sh

tuning-header-test: $(TEST_NAME) $(NAME)
	./$(TEST_NAME)
	ENGINE=./$(NAME) ./scripts/test_uci_profile_identity.sh

$(TEST_NAME): $(TEST_OBJS)
	$(CXX) $(TEST_CXXFLAGS) $(LDFLAGS) $(TEST_OBJS) $(LDLIBS) -o $(TEST_NAME)

print-config:
	@echo "CXX=$(CXX)"
	@echo "CXXFLAGS=$(CXXFLAGS)"
	@echo "TEST_CXXFLAGS=$(TEST_CXXFLAGS)"
	@echo "LDFLAGS=$(LDFLAGS)"
	@echo "LDLIBS=$(LDLIBS)"

candidate-build:
	@test -n "$(CANDIDATE_HEADER)" || (echo "CANDIDATE_HEADER is required" >&2; exit 2)
	@test -n "$(CANDIDATE_OUTPUT)" || (echo "CANDIDATE_OUTPUT is required" >&2; exit 2)
	@mkdir -p "$(dir $(CANDIDATE_OUTPUT))"
	$(CXX) $(CXXFLAGS) -DBITBOARD_TUNING_HEADER='"$(abspath $(CANDIDATE_HEADER))"' $(SRCS) $(LDLIBS) -o "$(CANDIDATE_OUTPUT)"

release-build:
	@test -n "$(RELEASE_ID)" || (echo "RELEASE_ID is required" >&2; exit 2)
	@test -n "$(PROFILE_HEADER)" || (echo "PROFILE_HEADER is required" >&2; exit 2)
	@test -n "$(OUTPUT)" || (echo "OUTPUT is required" >&2; exit 2)
	@printf '%s' "$(RELEASE_ID)" | grep -Eq '^[a-z0-9][a-z0-9.-]*$$' || (echo "invalid RELEASE_ID" >&2; exit 2)
	@case "$(RELEASE_ID)" in *..*|*.) echo "invalid RELEASE_ID" >&2; exit 2;; esac
	@case "$(notdir $(OUTPUT))" in *"$(RELEASE_ID)"*) ;; *) echo "OUTPUT executable name must include RELEASE_ID" >&2; exit 2;; esac
	@mkdir -p "$(dir $(OUTPUT))"
	$(CXX) $(CXXFLAGS) -DBITBOARD_TUNING_HEADER='"$(abspath $(PROFILE_HEADER))"' $(SRCS) $(LDLIBS) -o "$(OUTPUT)"

.PHONY: all clean fclean re test tuning-header-test print-config candidate-build release-build
