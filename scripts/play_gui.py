#!/usr/bin/env python3
"""
Simple Pygame GUI to play against a UCI chess engine.
"""

import argparse
import os
import sys
import threading
import pygame
import chess
import chess.engine

# Settings
SQUARE_SIZE = 120
BOARD_SIZE = SQUARE_SIZE * 8
WINDOW_SIZE = (BOARD_SIZE, BOARD_SIZE + 40) # Extra space for status bar
FPS = 60

# Colors
COLOR_LIGHT = (240, 217, 181)
COLOR_DARK = (181, 136, 99)
COLOR_HIGHLIGHT = (205, 210, 106)
COLOR_STATUS = (40, 40, 40)
COLOR_TEXT = (255, 255, 255)

TEXTURES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets/pieces")
IMAGES = {}

def load_images():
    if not os.path.exists(TEXTURES_DIR):
        print(f"Error: The 'textures/' directory is missing at {TEXTURES_DIR}")
        print("Please create it and place the following 12 PNG files inside:")
        print("  wK.png, wQ.png, wR.png, wB.png, wN.png, wP.png")
        print("  bK.png, bQ.png, bR.png, bB.png, bN.png, bP.png")
        sys.exit(1)

    pieces = ['wK', 'wQ', 'wR', 'wB', 'wN', 'wP', 'bK', 'bQ', 'bR', 'bB', 'bN', 'bP']
    missing = []
    
    for piece in pieces:
        path = os.path.join(TEXTURES_DIR, f"{piece}.png")
        if os.path.exists(path):
            image = pygame.image.load(path)
            # Scale to fit square size exactly
            IMAGES[piece] = pygame.transform.smoothscale(image, (SQUARE_SIZE, SQUARE_SIZE))
        else:
            missing.append(f"{piece}.png")
            
    if missing:
        print(f"Error: Missing image files in the 'textures/' directory.")
        print(f"Please ensure the following files are present:")
        for m in missing:
            print(f"  - {m}")
        sys.exit(1)

def get_piece_str(piece):
    if not piece:
        return None
    color = 'w' if piece.color == chess.WHITE else 'b'
    ptype = piece.symbol().upper()
    return f"{color}{ptype}"

# Thread-safe variables for engine move
engine_move = None
thinking = False
engine_error = None

def get_engine_move(engine, board, time_limit):
    global engine_move, thinking, engine_error
    try:
        result = engine.play(board, chess.engine.Limit(time=time_limit))
        engine_move = result.move
    except Exception as e:
        engine_error = str(e)
    finally:
        thinking = False

def start_animation(move, piece, anim_state):
    start_c = chess.square_file(move.from_square)
    start_r = 7 - chess.square_rank(move.from_square)
    end_c = chess.square_file(move.to_square)
    end_r = 7 - chess.square_rank(move.to_square)
    
    anim_state["active"] = True
    anim_state["piece"] = piece
    anim_state["start_pos"] = (start_c * SQUARE_SIZE, start_r * SQUARE_SIZE)
    anim_state["target_pos"] = (end_c * SQUARE_SIZE, end_r * SQUARE_SIZE)
    anim_state["start_time"] = pygame.time.get_ticks()
    anim_state["duration"] = 150 # ms
    anim_state["move"] = move

def draw_board(screen, board, selected_square, anim_state):
    for r in range(8):
        for c in range(8):
            color = COLOR_LIGHT if (r + c) % 2 == 0 else COLOR_DARK
            rect = pygame.Rect(c * SQUARE_SIZE, r * SQUARE_SIZE, SQUARE_SIZE, SQUARE_SIZE)
            pygame.draw.rect(screen, color, rect)

            # Draw highlight
            square = chess.square(c, 7 - r)
            if square == selected_square:
                pygame.draw.rect(screen, COLOR_HIGHLIGHT, rect)

            # Draw piece
            piece = board.piece_at(square)
            
            # Skip drawing the destination square of the animating piece
            if anim_state["active"] and square == anim_state["move"].to_square:
                continue
                
            piece_str = get_piece_str(piece)
            if piece_str:
                screen.blit(IMAGES[piece_str], rect)

def draw_animation(screen, anim_state):
    if not anim_state["active"]:
        return
        
    now = pygame.time.get_ticks()
    elapsed = now - anim_state["start_time"]
    progress = min(1.0, elapsed / anim_state["duration"])
    
    # Interpolate
    start_x, start_y = anim_state["start_pos"]
    end_x, end_y = anim_state["target_pos"]
    
    current_x = start_x + (end_x - start_x) * progress
    current_y = start_y + (end_y - start_y) * progress
    
    piece_str = get_piece_str(anim_state["piece"])
    if piece_str:
        screen.blit(IMAGES[piece_str], (current_x, current_y))
        
    if progress >= 1.0:
        anim_state["active"] = False

def draw_promotion_ui(screen, color):
    # Dim the board
    overlay = pygame.Surface((BOARD_SIZE, BOARD_SIZE), pygame.SRCALPHA)
    overlay.fill((0, 0, 0, 150))
    screen.blit(overlay, (0, 0))
    
    # Draw 4 buttons in the center
    box_w = SQUARE_SIZE * 4
    box_h = SQUARE_SIZE
    start_x = (BOARD_SIZE - box_w) // 2
    start_y = (BOARD_SIZE - box_h) // 2
    
    pygame.draw.rect(screen, (200, 200, 200), (start_x - 5, start_y - 5, box_w + 10, box_h + 10), border_radius=10)
    
    pieces = [chess.QUEEN, chess.ROOK, chess.BISHOP, chess.KNIGHT]
    color_prefix = 'w' if color == chess.WHITE else 'b'
    symbols = ['Q', 'R', 'B', 'N']
    
    for i, p in enumerate(pieces):
        rect = pygame.Rect(start_x + i * SQUARE_SIZE, start_y, SQUARE_SIZE, SQUARE_SIZE)
        pygame.draw.rect(screen, COLOR_LIGHT if i % 2 == 0 else COLOR_DARK, rect)
        
        piece_str = f"{color_prefix}{symbols[i]}"
        if piece_str in IMAGES:
            screen.blit(IMAGES[piece_str], rect)

def draw_game_over(screen, board, font_large):
    overlay = pygame.Surface((BOARD_SIZE, BOARD_SIZE), pygame.SRCALPHA)
    overlay.fill((0, 0, 0, 180))
    screen.blit(overlay, (0, 0))
    
    result = board.result()
    if result == "1-0":
        text = "White Wins!"
    elif result == "0-1":
        text = "Black Wins!"
    else:
        text = "Draw!"
        
    outcome = board.outcome()
    reason = ""
    if outcome:
        if outcome.termination == chess.Termination.CHECKMATE:
            reason = "Checkmate"
        elif outcome.termination == chess.Termination.STALEMATE:
            reason = "Stalemate"
        elif outcome.termination == chess.Termination.INSUFFICIENT_MATERIAL:
            reason = "Insufficient Material"
        elif outcome.termination == chess.Termination.SEVENTYFIVE_MOVES:
            reason = "75-Move Rule"
        elif outcome.termination == chess.Termination.FIVEFOLD_REPETITION:
            reason = "5-Fold Repetition"
        elif outcome.termination == chess.Termination.FIFTY_MOVES:
            reason = "50-Move Rule"
        elif outcome.termination == chess.Termination.THREEFOLD_REPETITION:
            reason = "3-Fold Repetition"
            
    if reason:
        text += f" ({reason})"
        
    text_surf = font_large.render(text, True, (255, 255, 255))
    text_rect = text_surf.get_rect(center=(BOARD_SIZE // 2, BOARD_SIZE // 2))
    
    # Draw background for text to make it readable
    bg_rect = text_rect.inflate(40, 20)
    pygame.draw.rect(screen, (40, 40, 40), bg_rect, border_radius=10)
    pygame.draw.rect(screen, (200, 200, 200), bg_rect, width=3, border_radius=10)
    
    screen.blit(text_surf, text_rect)

def draw_status(screen, font, message):
    rect = pygame.Rect(0, BOARD_SIZE, BOARD_SIZE, 40)
    pygame.draw.rect(screen, COLOR_STATUS, rect)
    text = font.render(message, True, COLOR_TEXT)
    screen.blit(text, (10, BOARD_SIZE + 10))

def main():
    global engine_move, thinking, engine_error

    parser = argparse.ArgumentParser(description="Play against a UCI engine via GUI.")
    parser.add_argument("--engine", required=True, help="Path to the UCI engine binary.")
    parser.add_argument("--time", type=float, default=0.5, help="Engine thinking time per move in seconds (default: 0.5).")
    args = parser.parse_args()

    engine_path = os.path.abspath(args.engine)
    if not os.path.exists(engine_path):
        print(f"Error: Engine not found at {engine_path}")
        sys.exit(1)

    pygame.init()
    screen = pygame.display.set_mode(WINDOW_SIZE)
    pygame.display.set_caption("Chess Engine GUI")
    clock = pygame.time.Clock()
    font = pygame.font.SysFont(None, 32)
    font_large = pygame.font.SysFont(None, max(48, SQUARE_SIZE // 3))

    load_images()

    board = chess.Board()
    engine = chess.engine.SimpleEngine.popen_uci([engine_path, "--mode=gui"])
    
    # Engine is black by default
    human_color = chess.WHITE
    
    selected_square = None
    running = True
    
    anim_state = {"active": False}
    promotion_state = False
    promotion_pending_move = None

    while running:
        # Handle engine move once animation is done
        if not thinking and engine_move is not None and not anim_state["active"]:
            move = engine_move
            piece = board.piece_at(move.from_square)
            start_animation(move, piece, anim_state)
            board.push(move)
            engine_move = None

        if engine_error:
            print(f"Engine Error: {engine_error}")
            running = False
            break

        game_over = board.is_game_over()
        
        if game_over:
            status = f"Game Over! Result: {board.result()}"
        elif thinking:
            status = "Engine is thinking..."
        elif promotion_state:
            status = "Choose promotion piece"
        elif board.turn == human_color:
            status = "Your turn (White)"
        else:
            status = "Engine's turn (Black)"

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                pos = pygame.mouse.get_pos()
                
                # Handle promotion clicks
                if promotion_state:
                    box_w = SQUARE_SIZE * 4
                    box_h = SQUARE_SIZE
                    start_x = (BOARD_SIZE - box_w) // 2
                    start_y = (BOARD_SIZE - box_h) // 2
                    
                    if start_y <= pos[1] <= start_y + box_h and start_x <= pos[0] <= start_x + box_w:
                        idx = int((pos[0] - start_x) // SQUARE_SIZE)
                        pieces = [chess.QUEEN, chess.ROOK, chess.BISHOP, chess.KNIGHT]
                        chosen_piece = pieces[idx]
                        
                        move = chess.Move(promotion_pending_move[0], promotion_pending_move[1], promotion=chosen_piece)
                        
                        piece = board.piece_at(promotion_pending_move[0])
                        start_animation(move, piece, anim_state)
                        board.push(move)
                        
                        promotion_state = False
                        promotion_pending_move = None
                        
                        if not board.is_game_over():
                            thinking = True
                            threading.Thread(
                                target=get_engine_move, 
                                args=(engine, board.copy(), args.time), 
                                daemon=True
                            ).start()
                    else:
                        # Clicked outside, cancel promotion
                        promotion_state = False
                        promotion_pending_move = None
                        selected_square = None
                        
                    continue
                
                # Normal move clicks
                if anim_state["active"] or game_over or thinking or board.turn != human_color:
                    continue
                
                if pos[1] >= BOARD_SIZE:
                    continue # Clicked in status bar
                
                c = pos[0] // SQUARE_SIZE
                r = pos[1] // SQUARE_SIZE
                clicked_square = chess.square(c, 7 - r)

                if selected_square is None:
                    if board.piece_at(clicked_square) and board.piece_at(clicked_square).color == human_color:
                        selected_square = clicked_square
                else:
                    move = chess.Move(selected_square, clicked_square)
                    piece = board.piece_at(selected_square)
                    
                    is_promotion = False
                    if piece and piece.piece_type == chess.PAWN:
                        if chess.square_rank(clicked_square) == 0 or chess.square_rank(clicked_square) == 7:
                            is_promotion = True
                            
                    if is_promotion:
                        # Check if it's a legal move by testing with a Queen
                        test_move = chess.Move(selected_square, clicked_square, promotion=chess.QUEEN)
                        if test_move in board.legal_moves:
                            promotion_pending_move = (selected_square, clicked_square)
                            promotion_state = True
                            selected_square = None
                        else:
                            selected_square = None
                    else:
                        if move in board.legal_moves:
                            start_animation(move, piece, anim_state)
                            board.push(move)
                            selected_square = None
                            
                            if not board.is_game_over():
                                thinking = True
                                threading.Thread(
                                    target=get_engine_move, 
                                    args=(engine, board.copy(), args.time), 
                                    daemon=True
                                ).start()
                        else:
                            # Re-select if clicking another own piece
                            if board.piece_at(clicked_square) and board.piece_at(clicked_square).color == human_color:
                                selected_square = clicked_square
                            else:
                                selected_square = None

        screen.fill(COLOR_LIGHT)
        draw_board(screen, board, selected_square, anim_state)
        
        # Draw legal move indicators
        if selected_square is not None and not promotion_state and not anim_state["active"]:
            surface = pygame.Surface((SQUARE_SIZE, SQUARE_SIZE), pygame.SRCALPHA)
            pygame.draw.circle(surface, (0, 0, 0, 50), (SQUARE_SIZE // 2, SQUARE_SIZE // 2), SQUARE_SIZE // 6)
            for move in board.legal_moves:
                if move.from_square == selected_square:
                    to_square = move.to_square
                    c = chess.square_file(to_square)
                    r = 7 - chess.square_rank(to_square)
                    screen.blit(surface, (c * SQUARE_SIZE, r * SQUARE_SIZE))
                    
        if anim_state["active"]:
            draw_animation(screen, anim_state)
            
        if promotion_state:
            draw_promotion_ui(screen, board.turn)
            
        if game_over and not anim_state["active"]:
            draw_game_over(screen, board, font_large)

        draw_status(screen, font, status)

        pygame.display.flip()
        clock.tick(FPS)

    engine.quit()
    pygame.quit()

if __name__ == "__main__":
    main()
