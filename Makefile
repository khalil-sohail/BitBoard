NAME =  chess-engine
TEST_NAME = chess-engine-tests

SRCS = src/*.cpp src/app/*.cpp src/eval/*.cpp src/search/*.cpp src/move/*.cpp
ENGINE_SRCS = src/Board.cpp src/openingBook.cpp src/printers.cpp src/app/*.cpp src/eval/*.cpp src/search/*.cpp src/move/*.cpp
TEST_SRCS = $(ENGINE_SRCS) tests.cpp

CC = g++

CFLAGS = -Wall -Wextra -I./include -std=c++23 -fsanitize=address -g3
TEST_CFLAGS = -Wall -Wextra -I./include -std=c++23

RM = rm -rf

all : $(NAME)

$(NAME): $(SRCS) clean
	$(CC) $(CFLAGS) $(SRCS) -o $(NAME)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean :
		@$(RM)

fclean : clean
		@$(RM) $(NAME)
		@$(RM) $(TEST_NAME)

re : fclean all

test: $(TEST_NAME)
	./$(TEST_NAME)

$(TEST_NAME): $(TEST_SRCS)
	$(CC) $(TEST_CFLAGS) $(TEST_SRCS) -o $(TEST_NAME)

.PHONY: all clean fclean re test
