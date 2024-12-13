NAME =  chess-engine

SRCS = src/*.cpp

CC = g++

CFLAGS = -Wall -Wextra -I./include -std=c++23 -fsanitize=address -g3
LFLAGS = -lsfml-graphics -lsfml-window -lsfml-system

RM = rm -rf

all : $(NAME)

$(NAME): $(SRCS) clean
	$(CC) $(CFLAGS) $(SRCS) -o $(NAME) $(LFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean :
		@$(RM)

fclean : clean
		@$(RM) $(NAME)

re : fclean all
