NAME =  btc

SRCS = src/*.cpp

OBJ = $(SRCS:.cpp=.o)

CC = g++

CFLAGS = -Wall -Wextra  -I./include -std=c++23

RM = rm -rf

all : $(NAME)

$(NAME): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(NAME)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean :
		@$(RM) $(OBJ)

fclean : clean
		@$(RM) $(NAME)

re : fclean all
