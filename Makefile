NAME =  chess-engine

SRCS = src/*.cpp

CC = g++

CFLAGS = -Wall -Wextra  -I./include -std=c++23

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

re : fclean all
