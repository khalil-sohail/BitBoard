NAME =  chess-engine
TEST_NAME = chess-engine-tests
TUNE_NAME = chess-engine-tune

SRCS = $(wildcard src/*.cpp src/app/*.cpp src/eval/*.cpp src/search/*.cpp src/move/*.cpp)
ENGINE_SRCS = src/Board.cpp src/openingBook.cpp src/printers.cpp $(wildcard src/app/*.cpp src/eval/*.cpp src/search/*.cpp src/move/*.cpp)
TEST_SRCS = $(ENGINE_SRCS) tests.cpp
TUNE_SRCS = $(ENGINE_SRCS) src/tuning/eval_param_registry.cpp src/tuning/texel_main.cpp

OBJS = $(SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
TUNE_OBJS = $(TUNE_SRCS:.cpp=.o)

CC = g++

CFLAGS = -Wall -Wextra -I./include -std=c++2b -O3 -march=native -flto -DNDEBUG -funroll-loops -lpthread
TEST_CFLAGS = -Wall -Wextra -I./include -std=c++2b -lpthread
TUNE_CFLAGS = -Wall -Wextra -I./include -std=c++2b -O2 -DEVAL_TUNING_MODE -DNDEBUG -lpthread

RM = rm -rf

all : $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean :
	@$(RM) $(OBJS) $(TEST_OBJS) $(TUNE_OBJS)

fclean : clean
	@$(RM) $(NAME) $(TEST_NAME) $(TUNE_NAME)

re : fclean all

test: $(TEST_NAME)
	./$(TEST_NAME)

$(TEST_NAME): $(TEST_OBJS)
	$(CC) $(TEST_CFLAGS) $(TEST_OBJS) -o $(TEST_NAME)

tune: $(TUNE_NAME)

$(TUNE_NAME): $(TUNE_OBJS)
	$(CC) $(TUNE_CFLAGS) $(TUNE_OBJS) -o $(TUNE_NAME)

.PHONY: all clean fclean re test tune
