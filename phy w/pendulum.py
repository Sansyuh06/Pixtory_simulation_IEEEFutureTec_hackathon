import pygame
import numpy as np
import threading
import time
import math
from typing import Tuple, Dict, Any, List
import colorsys

# Initialize Pygame
pygame.init()
pygame.mixer.init(frequency=22050, size=-16, channels=2, buffer=512)

# Constants
WINDOW_WIDTH = 1600
WINDOW_HEIGHT = 900
MAP_WIDTH = 1000
MAP_HEIGHT = 800
PANEL_WIDTH = 600
MAP_RESOLUTION = 800

# Physical constants (matching typical double pendulum)
L1, L2 = 1.0, 1.0  # Length of pendulum arms
M1, M2 = 1.0, 1.0  # Masses
G = 9.81  # Gravity

# Professional color scheme
BACKGROUND = (15, 15, 20)
PANEL_BG = (25, 25, 30)
ACCENT_BLUE = (100, 150, 255)
ACCENT_GREEN = (100, 255, 150)
ACCENT_RED = (255, 100, 100)
ACCENT_YELLOW = (255, 200, 100)
WHITE = (255, 255, 255)
GRAY = (128, 128, 128)
DARK_GRAY = (60, 60, 65)

class AudioManager:
    def __init__(self):
        self.is_playing = False
        self.freq1 = 220.0
        self.freq2 = 330.0
        self.sound_thread = None
        self.stop_flag = False
        
    def start_audio(self):
        if not self.is_playing:
            self.is_playing = True
            self.stop_flag = False
            self.sound_thread = threading.Thread(target=self._audio_loop, daemon=True)
            self.sound_thread.start()
    
    def stop_audio(self):
        self.is_playing = False
        self.stop_flag = True
        if self.sound_thread:
            self.sound_thread.join(timeout=0.1)
    
    def update_frequencies(self, omega1: float, omega2: float):
        self.freq1 = 220 + abs(omega1) * 30
        self.freq2 = 330 + abs(omega2) * 40
    
    def _audio_loop(self):
        sample_rate = 22050
        duration = 0.1
        
        while self.is_playing and not self.stop_flag:
            try:
                samples1 = np.sin(2 * np.pi * self.freq1 * np.linspace(0, duration, int(sample_rate * duration)))
                samples2 = np.sin(2 * np.pi * self.freq2 * np.linspace(0, duration, int(sample_rate * duration)))
                
                combined = (samples1 * 0.1 + samples2 * 0.1) * 0.3
                combined = (combined * 32767).astype(np.int16)
                
                stereo = np.zeros((len(combined), 2), dtype=np.int16)
                stereo[:, 0] = combined
                stereo[:, 1] = combined
                
                sound = pygame.sndarray.make_sound(stereo)
                sound.play()
                time.sleep(duration * 0.8)
            except:
                break

class DoublePendulum:
    def __init__(self):
        self.reset()
        
    def reset(self):
        self.theta1 = 0.0
        self.theta2 = 0.0
        self.omega1 = 0.0
        self.omega2 = 0.0
        self.trail = []
        self.time = 0.0
        self.energy_history = []
        
    def step(self, dt: float):
        """Runge-Kutta 4th order integration for better accuracy"""
        def derivatives(theta1, theta2, omega1, omega2):
            delta = theta2 - theta1
            den1 = (M1 + M2) * L1 - M2 * L1 * math.cos(delta) * math.cos(delta)
            den2 = (L2 / L1) * den1
            
            # Equations of motion
            num1 = (-M2 * L1 * omega1**2 * math.sin(delta) * math.cos(delta) +
                    M2 * G * math.sin(theta2) * math.cos(delta) +
                    M2 * L2 * omega2**2 * math.sin(delta) -
                    (M1 + M2) * G * math.sin(theta1))
            
            num2 = (-M2 * L2 * omega2**2 * math.sin(delta) * math.cos(delta) +
                    (M1 + M2) * G * math.sin(theta1) * math.cos(delta) -
                    (M1 + M2) * L1 * omega1**2 * math.sin(delta) -
                    (M1 + M2) * G * math.sin(theta2))
            
            dtheta1_dt = omega1
            dtheta2_dt = omega2
            domega1_dt = num1 / den1
            domega2_dt = num2 / den2
            
            return dtheta1_dt, dtheta2_dt, domega1_dt, domega2_dt
        
        # RK4 integration
        k1 = derivatives(self.theta1, self.theta2, self.omega1, self.omega2)
        k2 = derivatives(
            self.theta1 + 0.5 * dt * k1[0],
            self.theta2 + 0.5 * dt * k1[1],
            self.omega1 + 0.5 * dt * k1[2],
            self.omega2 + 0.5 * dt * k1[3]
        )
        k3 = derivatives(
            self.theta1 + 0.5 * dt * k2[0],
            self.theta2 + 0.5 * dt * k2[1],
            self.omega1 + 0.5 * dt * k2[2],
            self.omega2 + 0.5 * dt * k2[3]
        )
        k4 = derivatives(
            self.theta1 + dt * k3[0],
            self.theta2 + dt * k3[1],
            self.omega1 + dt * k3[2],
            self.omega2 + dt * k3[3]
        )
        
        # Update state
        self.theta1 += (dt / 6.0) * (k1[0] + 2*k2[0] + 2*k3[0] + k4[0])
        self.theta2 += (dt / 6.0) * (k1[1] + 2*k2[1] + 2*k3[1] + k4[1])
        self.omega1 += (dt / 6.0) * (k1[2] + 2*k2[2] + 2*k3[2] + k4[2])
        self.omega2 += (dt / 6.0) * (k1[3] + 2*k2[3] + 2*k3[3] + k4[3])
        
        # Calculate positions for visualization
        scale = 200  # Scale for display
        center_x, center_y = 200, 150
        
        x1 = center_x + scale * L1 * math.sin(self.theta1)
        y1 = center_y + scale * L1 * math.cos(self.theta1)
        x2 = x1 + scale * L2 * math.sin(self.theta2)
        y2 = y1 + scale * L2 * math.cos(self.theta2)
        
        self.trail.append((x2, y2))
        if len(self.trail) > 1000:
            self.trail.pop(0)
        
        # Energy tracking
        energy = self.calculate_energy()
        self.energy_history.append(energy)
        if len(self.energy_history) > 500:
            self.energy_history.pop(0)
        
        self.time += dt
    
    def calculate_energy(self) -> float:
        """Calculate total mechanical energy"""
        # Kinetic energy
        v1_x = L1 * self.omega1 * math.cos(self.theta1)
        v1_y = -L1 * self.omega1 * math.sin(self.theta1)
        
        v2_x = v1_x + L2 * self.omega2 * math.cos(self.theta2)
        v2_y = v1_y - L2 * self.omega2 * math.sin(self.theta2)
        
        kinetic1 = 0.5 * M1 * (v1_x**2 + v1_y**2)
        kinetic2 = 0.5 * M2 * (v2_x**2 + v2_y**2)
        
        # Potential energy (taking center as reference)
        potential1 = -M1 * G * L1 * math.cos(self.theta1)
        potential2 = -M2 * G * (L1 * math.cos(self.theta1) + L2 * math.cos(self.theta2))
        
        return kinetic1 + kinetic2 + potential1 + potential2
    
    def set_initial_conditions(self, theta1: float, theta2: float):
        self.theta1 = theta1
        self.theta2 = theta2
        self.omega1 = 0.0
        self.omega2 = 0.0
        self.time = 0.0
        self.trail.clear()
        self.energy_history.clear()

class ChaosMapGenerator:
    def __init__(self):
        self.resolution = MAP_RESOLUTION
        self.chaos_map = None
        self.flip_time_map = None
        self.is_generating = False
        self.progress = 0.0
        self.generation_cancelled = False
        
        # Map parameters
        self.theta1_range = (-math.pi, math.pi)
        self.theta2_range = (-math.pi, math.pi)
        self.max_simulation_time = 20.0  # seconds
        self.dt = 0.005
        self.flip_threshold = math.pi  # When pendulum "flips"
        
    def generate_chaos_map(self, theta1_range: Tuple[float, float], theta2_range: Tuple[float, float]):
        """Generate the chaos map showing flip times for different initial conditions"""
        if self.is_generating:
            self.generation_cancelled = True
            return
        
        self.is_generating = True
        self.generation_cancelled = False
        self.progress = 0.0
        self.theta1_range = theta1_range
        self.theta2_range = theta2_range
        
        def generate_thread():
            width = height = self.resolution
            flip_times = np.full((height, width), self.max_simulation_time, dtype=np.float32)
            chaos_map = np.zeros((height, width, 3), dtype=np.uint8)
            
            theta1_span = theta1_range[1] - theta1_range[0]
            theta2_span = theta2_range[1] - theta2_range[0]
            
            # Process in chunks for better performance and responsiveness
            chunk_size = 4
            total_chunks = (height // chunk_size) * (width // chunk_size)
            processed_chunks = 0
            
            for y in range(0, height, chunk_size):
                if self.generation_cancelled:
                    self.is_generating = False
                    return
                
                y_end = min(y + chunk_size, height)
                
                for x in range(0, width, chunk_size):
                    if self.generation_cancelled:
                        self.is_generating = False
                        return
                    
                    x_end = min(x + chunk_size, width)
                    
                    # Calculate initial conditions for this chunk
                    theta1 = theta1_range[0] + (x + chunk_size/2) / width * theta1_span
                    theta2 = theta2_range[1] - (y + chunk_size/2) / height * theta2_span
                    
                    # Simulate to find flip time
                    flip_time = self._simulate_until_flip(theta1, theta2)
                    
                    # Calculate color based on flip time
                    r, g, b = self._flip_time_to_color(flip_time)
                    
                    # Fill the chunk
                    flip_times[y:y_end, x:x_end] = flip_time
                    chaos_map[y:y_end, x:x_end] = [r, g, b]
                    
                    processed_chunks += 1
                    self.progress = processed_chunks / total_chunks
            
            self.flip_time_map = flip_times
            self.chaos_map = chaos_map
            self.is_generating = False
        
        threading.Thread(target=generate_thread, daemon=True).start()
    
    def _simulate_until_flip(self, theta1_init: float, theta2_init: float) -> float:
        """Simulate pendulum until it flips over, return time taken"""
        theta1, theta2 = theta1_init, theta2_init
        omega1, omega2 = 0.0, 0.0
        
        # Track initial positions to detect flips
        initial_y1 = math.cos(theta1)
        initial_y2 = math.cos(theta1) + math.cos(theta2)
        
        t = 0.0
        max_steps = int(self.max_simulation_time / self.dt)
        
        for step in range(max_steps):
            # Check for flip condition
            current_y1 = math.cos(theta1)
            current_y2 = math.cos(theta1) + math.cos(theta2)
            
            # Flip detected if either pendulum crosses over the top
            if (initial_y1 > 0 and current_y1 < -0.9) or (initial_y2 > 0 and current_y2 < -0.9):
                return t
            
            # Integration step using simplified equations for speed
            delta = theta2 - theta1
            den1 = (M1 + M2) * L1 - M2 * L1 * math.cos(delta) * math.cos(delta)
            den2 = (L2 / L1) * den1
            
            if abs(den1) < 1e-10 or abs(den2) < 1e-10:
                break
            
            num1 = (-M2 * L1 * omega1**2 * math.sin(delta) * math.cos(delta) +
                    M2 * G * math.sin(theta2) * math.cos(delta) +
                    M2 * L2 * omega2**2 * math.sin(delta) -
                    (M1 + M2) * G * math.sin(theta1))
            
            num2 = (-M2 * L2 * omega2**2 * math.sin(delta) * math.cos(delta) +
                    (M1 + M2) * G * math.sin(theta1) * math.cos(delta) -
                    (M1 + M2) * L1 * omega1**2 * math.sin(delta) -
                    (M1 + M2) * G * math.sin(theta2))
            
            domega1_dt = num1 / den1
            domega2_dt = num2 / den2
            
            # Update state
            theta1 += omega1 * self.dt
            theta2 += omega2 * self.dt
            omega1 += domega1_dt * self.dt
            omega2 += domega2_dt * self.dt
            
            t += self.dt
        
        return self.max_simulation_time  # No flip within time limit
    
    def _flip_time_to_color(self, flip_time: float) -> Tuple[int, int, int]:
        """Convert flip time to color using the classic chaos map coloring"""
        if flip_time >= self.max_simulation_time:
            # Never flipped - white/very light
            return (240, 240, 255)
        
        # Normalize flip time to [0, 1]
        normalized_time = flip_time / self.max_simulation_time
        
        # Classic chaos map coloring scheme
        if normalized_time < 0.1:
            # Very fast flip - red
            intensity = (0.1 - normalized_time) / 0.1
            return (int(255 * intensity), 0, 0)
        elif normalized_time < 0.3:
            # Fast flip - orange to yellow
            t = (normalized_time - 0.1) / 0.2
            return (255, int(150 + 105 * t), 0)
        elif normalized_time < 0.6:
            # Medium flip - green
            t = (normalized_time - 0.3) / 0.3
            return (int(255 * (1 - t)), 255, 0)
        elif normalized_time < 0.8:
            # Slow flip - blue-green
            t = (normalized_time - 0.6) / 0.2
            return (0, int(255 * (1 - t)), int(255 * t))
        else:
            # Very slow flip - blue to purple
            t = (normalized_time - 0.8) / 0.2
            return (int(128 * t), 0, 255)

class DoublePendulumChaosExplorer:
    def __init__(self):
        self.screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
        pygame.display.set_caption("Double Pendulum Chaos Map Explorer - Basin of Attraction")
        self.clock = pygame.time.Clock()
        
        # Fonts
        self.font_title = pygame.font.Font(None, 32)
        self.font_subtitle = pygame.font.Font(None, 24)
        self.font_label = pygame.font.Font(None, 20)
        self.font_value = pygame.font.Font(None, 18)
        
        # Components
        self.pendulum = DoublePendulum()
        self.chaos_gen = ChaosMapGenerator()
        self.audio = AudioManager()
        
        # View state
        self.zoom = 1.0
        self.pan_x = 0.0
        self.pan_y = 0.0
        self.is_dragging = False
        self.last_mouse_pos = (0, 0)
        self.current_theta1 = 0.0
        self.current_theta2 = 0.0
        self.is_simulating = False
        self.show_crosshair = True
        self.audio_enabled = True
        
        # Surfaces
        self.chaos_surface = pygame.Surface((MAP_RESOLUTION, MAP_RESOLUTION))
        self.chaos_surface.fill(BACKGROUND)
        
        # Generate initial chaos map
        self.generate_initial_map()
    
    def generate_initial_map(self):
        """Generate the initial chaos map"""
        self.chaos_gen.generate_chaos_map((-math.pi, math.pi), (-math.pi, math.pi))
    
    def handle_events(self):
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                return False
            
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_r:
                    self.regenerate_map()
                elif event.key == pygame.K_SPACE:
                    self.reset_view()
                elif event.key == pygame.K_ESCAPE:
                    self.stop_simulation()
                elif event.key == pygame.K_a:
                    self.audio_enabled = not self.audio_enabled
                    if not self.audio_enabled:
                        self.audio.stop_audio()
                elif event.key == pygame.K_c:
                    self.show_crosshair = not self.show_crosshair
                elif event.key == pygame.K_EQUALS or event.key == pygame.K_PLUS:
                    self.zoom_in()
                elif event.key == pygame.K_MINUS:
                    self.zoom_out()
            
            elif event.type == pygame.MOUSEBUTTONDOWN:
                if event.button == 1:  # Left click
                    self.handle_left_click(event.pos)
                elif event.button == 3:  # Right click
                    self.stop_simulation()
                elif event.button == 4:  # Wheel up
                    self.zoom_action(1.2, *event.pos)
                elif event.button == 5:  # Wheel down
                    self.zoom_action(0.8, *event.pos)
            
            elif event.type == pygame.MOUSEBUTTONUP:
                if event.button == 1:
                    self.is_dragging = False
            
            elif event.type == pygame.MOUSEMOTION:
                if self.is_dragging:
                    dx = event.pos[0] - self.last_mouse_pos[0]
                    dy = event.pos[1] - self.last_mouse_pos[1]
                    self.pan_x += dx
                    self.pan_y += dy
                
                self.last_mouse_pos = event.pos
                self.update_mouse_coordinates(*event.pos)
        
        return True
    
    def handle_left_click(self, pos):
        mouse_x, mouse_y = pos
        
        # Check if clicking in map area
        if mouse_x < MAP_WIDTH and mouse_y > 100:
            # Convert screen coordinates to map coordinates
            map_x = (mouse_x - self.pan_x) / self.zoom
            map_y = (mouse_y - 100 - self.pan_y) / self.zoom
            
            if 0 <= map_x <= MAP_RESOLUTION and 0 <= map_y <= MAP_RESOLUTION:
                # Convert to mathematical coordinates
                theta1_span = self.chaos_gen.theta1_range[1] - self.chaos_gen.theta1_range[0]
                theta2_span = self.chaos_gen.theta2_range[1] - self.chaos_gen.theta2_range[0]
                
                theta1 = self.chaos_gen.theta1_range[0] + (map_x / MAP_RESOLUTION) * theta1_span
                theta2 = self.chaos_gen.theta2_range[1] - (map_y / MAP_RESOLUTION) * theta2_span
                
                self.start_simulation(theta1, theta2)
            else:
                # Start dragging
                self.is_dragging = True
        
        # Check toolbar buttons
        elif mouse_y <= 100:
            self.handle_toolbar_click(mouse_x, mouse_y)
    
    def handle_toolbar_click(self, x: int, y: int):
        # Regenerate button
        if 20 <= x <= 140 and 20 <= y <= 50:
            self.regenerate_map()
        # Reset button
        elif 150 <= x <= 240 and 20 <= y <= 50:
            self.reset_view()
        # Audio toggle
        elif 250 <= x <= 340 and 20 <= y <= 50:
            self.audio_enabled = not self.audio_enabled
            if not self.audio_enabled:
                self.audio.stop_audio()
        # Stop simulation
        elif 350 <= x <= 440 and 20 <= y <= 50:
            self.stop_simulation()
    
    def update_mouse_coordinates(self, mouse_x: int, mouse_y: int):
        if mouse_x < MAP_WIDTH and mouse_y > 100:
            map_x = (mouse_x - self.pan_x) / self.zoom
            map_y = (mouse_y - 100 - self.pan_y) / self.zoom
            
            if 0 <= map_x <= MAP_RESOLUTION and 0 <= map_y <= MAP_RESOLUTION:
                theta1_span = self.chaos_gen.theta1_range[1] - self.chaos_gen.theta1_range[0]
                theta2_span = self.chaos_gen.theta2_range[1] - self.chaos_gen.theta2_range[0]
                
                self.current_theta1 = self.chaos_gen.theta1_range[0] + (map_x / MAP_RESOLUTION) * theta1_span
                self.current_theta2 = self.chaos_gen.theta2_range[1] - (map_y / MAP_RESOLUTION) * theta2_span
    
    def zoom_action(self, factor: float, center_x: int, center_y: int):
        if center_y < 100:
            return
        
        new_zoom = max(0.1, min(20, self.zoom * factor))
        if new_zoom != self.zoom:
            world_x = (center_x - self.pan_x) / self.zoom
            world_y = (center_y - 100 - self.pan_y) / self.zoom
            
            self.zoom = new_zoom
            self.pan_x = center_x - world_x * self.zoom
            self.pan_y = center_y - 100 - world_y * self.zoom
            
            # Regenerate map at new zoom level if zoomed in significantly
            if self.zoom > 2.0:
                self.regenerate_zoomed_map()
    
    def zoom_in(self):
        self.zoom_action(1.5, MAP_WIDTH // 2, MAP_HEIGHT // 2 + 100)
    
    def zoom_out(self):
        self.zoom_action(1/1.5, MAP_WIDTH // 2, MAP_HEIGHT // 2 + 100)
    
    def reset_view(self):
        self.zoom = 1.0
        self.pan_x = 0.0
        self.pan_y = 0.0
        self.chaos_gen.theta1_range = (-math.pi, math.pi)
        self.chaos_gen.theta2_range = (-math.pi, math.pi)
        self.regenerate_map()
    
    def regenerate_map(self):
        if not self.chaos_gen.is_generating:
            self.chaos_gen.generate_chaos_map(self.chaos_gen.theta1_range, self.chaos_gen.theta2_range)
    
    def regenerate_zoomed_map(self):
        """Regenerate map for zoomed view with higher detail"""
        if not self.chaos_gen.is_generating and self.zoom > 2.0:
            # Calculate visible bounds
            visible_left = max(0, -self.pan_x / self.zoom)
            visible_top = max(0, -self.pan_y / self.zoom)
            visible_right = min(MAP_RESOLUTION, (-self.pan_x + MAP_WIDTH) / self.zoom)
            visible_bottom = min(MAP_RESOLUTION, (-self.pan_y + MAP_HEIGHT - 100) / self.zoom)
            
            # Convert to theta ranges
            theta1_span = self.chaos_gen.theta1_range[1] - self.chaos_gen.theta1_range[0]
            theta2_span = self.chaos_gen.theta2_range[1] - self.chaos_gen.theta2_range[0]
            
            new_theta1_min = self.chaos_gen.theta1_range[0] + (visible_left / MAP_RESOLUTION) * theta1_span
            new_theta1_max = self.chaos_gen.theta1_range[0] + (visible_right / MAP_RESOLUTION) * theta1_span
            new_theta2_min = self.chaos_gen.theta2_range[1] - (visible_bottom / MAP_RESOLUTION) * theta2_span
            new_theta2_max = self.chaos_gen.theta2_range[1] - (visible_top / MAP_RESOLUTION) * theta2_span
            
            self.chaos_gen.generate_chaos_map((new_theta1_min, new_theta1_max), (new_theta2_min, new_theta2_max))
    
    def start_simulation(self, theta1: float, theta2: float):
        self.pendulum.set_initial_conditions(theta1, theta2)
        if self.audio_enabled:
            self.audio.start_audio()
        self.is_simulating = True
    
    def stop_simulation(self):
        self.audio.stop_audio()
        self.is_simulating = False
    
    def update_simulation(self):
        if self.is_simulating:
            for _ in range(2):  # Run 2 steps per frame for smoother animation
                self.pendulum.step(0.008)
            
            if self.audio_enabled:
                self.audio.update_frequencies(self.pendulum.omega1, self.pendulum.omega2)
    
    def draw_professional_toolbar(self):
        # Toolbar background
        toolbar_rect = pygame.Rect(0, 0, MAP_WIDTH, 100)
        pygame.draw.rect(self.screen, PANEL_BG, toolbar_rect)
        pygame.draw.line(self.screen, ACCENT_BLUE, (0, 99), (MAP_WIDTH, 99), 2)
        
        # Title
        title = self.font_title.render("Double Pendulum Chaos Map - Basin of Attraction", True, WHITE)
        self.screen.blit(title, (20, 15))
        
        subtitle = self.font_subtitle.render("Colors show time until pendulum flips over", True, GRAY)
        self.screen.blit(subtitle, (20, 45))
        
        # Buttons
        buttons = [
            (20, 70, 120, 25, "Regenerate", not self.chaos_gen.is_generating, ACCENT_GREEN),
            (150, 70, 90, 25, "Reset View", True, ACCENT_BLUE),
            (250, 70, 90, 25, "Audio", self.audio_enabled, ACCENT_YELLOW if self.audio_enabled else DARK_GRAY),
            (350, 70, 90, 25, "Stop Sim", self.is_simulating, ACCENT_RED if self.is_simulating else DARK_GRAY)
        ]
        
        for x, y, w, h, text, enabled, color in buttons:
            self.draw_button(x, y, w, h, text, enabled, color)
        
        # Generation progress
        if self.chaos_gen.is_generating:
            progress_rect = pygame.Rect(500, 75, 200, 8)
            pygame.draw.rect(self.screen, DARK_GRAY, progress_rect)
            pygame.draw.rect(self.screen, WHITE, progress_rect, 1)
            
            fill_rect = pygame.Rect(500, 75, int(200 * self.chaos_gen.progress), 8)
            pygame.draw.rect(self.screen, ACCENT_YELLOW, fill_rect)
            
            progress_text = f"Generating: {int(self.chaos_gen.progress * 100)}%"
            progress_surface = self.font_value.render(progress_text, True, WHITE)
            self.screen.blit(progress_surface, (500, 55))
        
        # Zoom indicator
        zoom_text = f"Zoom: {self.zoom:.1f}x"
        zoom_surface = self.font_value.render(zoom_text, True, WHITE)
        self.screen.blit(zoom_surface, (MAP_WIDTH - 120, 75))
    
    def draw_button(self, x: int, y: int, width: int, height: int, text: str, enabled: bool, color):
        button_rect = pygame.Rect(x, y, width, height)
        
        if enabled:
            pygame.draw.rect(self.screen, color, button_rect)
            pygame.draw.rect(self.screen, WHITE, button_rect, 2)
            text_color = WHITE
        else:
            pygame.draw.rect(self.screen, DARK_GRAY, button_rect)
            pygame.draw.rect(self.screen, GRAY, button_rect, 1)
            text_color = GRAY
        
        text_surface = self.font_label.render(text, True, text_color)
        text_rect = text_surface.get_rect(center=button_rect.center)
        self.screen.blit(text_surface, text_rect)
    
    def draw_chaos_map(self):
        # Map viewport
        map_rect = pygame.Rect(0, 100, MAP_WIDTH, MAP_HEIGHT - 100)
        self.screen.fill(BACKGROUND, map_rect)
        
        # Draw the chaos map
        if self.chaos_gen.chaos_map is not None:
            # Update surface with new data
            try:
                chaos_array = np.transpose(self.chaos_gen.chaos_map, (1, 0, 2))
                pygame.surfarray.blit_array(self.chaos_surface, chaos_array)
            except:
                pass
            
            # Draw scaled and panned surface
            scaled_size = max(1, int(MAP_RESOLUTION * self.zoom))
            if scaled_size > 0:
                try:
                    scaled_surface = pygame.transform.scale(self.chaos_surface, (scaled_size, scaled_size))
                    self.screen.blit(scaled_surface, (int(self.pan_x), int(100 + self.pan_y)))
                except:
                    pass
        
        # Draw coordinate grid
        self.draw_coordinate_grid()
        
        # Draw crosshair at mouse position
        if self.show_crosshair:
            mouse_x, mouse_y = pygame.mouse.get_pos()
            if mouse_x < MAP_WIDTH and mouse_y > 100:
                pygame.draw.line(self.screen, WHITE, (mouse_x, 100), (mouse_x, MAP_HEIGHT), 1)
                pygame.draw.line(self.screen, WHITE, (0, mouse_y), (MAP_WIDTH, mouse_y), 1)
        
        # Current position indicator
        if self.is_simulating:
            # Calculate screen position of current simulation
            theta1_norm = (self.pendulum.theta1 - self.chaos_gen.theta1_range[0]) / (self.chaos_gen.theta1_range[1] - self.chaos_gen.theta1_range[0])
            theta2_norm = 1.0 - (self.pendulum.theta2 - self.chaos_gen.theta2_range[0]) / (self.chaos_gen.theta2_range[1] - self.chaos_gen.theta2_range[0])
            
            screen_x = self.pan_x + theta1_norm * MAP_RESOLUTION * self.zoom
            screen_y = 100 + self.pan_y + theta2_norm * MAP_RESOLUTION * self.zoom
            
            if 0 <= screen_x <= MAP_WIDTH and 100 <= screen_y <= MAP_HEIGHT:
                # Pulsing indicator
                pulse = int(128 + 127 * math.sin(time.time() * 10))
                pygame.draw.circle(self.screen, (255, pulse, pulse), (int(screen_x), int(screen_y)), 8, 3)
    
    def draw_coordinate_grid(self):
        # Only draw grid when zoomed in enough
        if self.zoom < 1.5:
            return
        
        # Calculate grid spacing based on zoom
        grid_spacing = max(50, 100 / self.zoom)
        
        # Vertical lines (theta1)
        start_x = self.pan_x % grid_spacing
        x = start_x
        while x < MAP_WIDTH:
            if x > 0:
                pygame.draw.line(self.screen, (255, 255, 255, 30), (int(x), 100), (int(x), MAP_HEIGHT))
            x += grid_spacing
        
        # Horizontal lines (theta2)
        start_y = (100 + self.pan_y) % grid_spacing
        y = start_y
        while y < MAP_HEIGHT:
            if y > 100:
                pygame.draw.line(self.screen, (255, 255, 255, 30), (0, int(y)), (MAP_WIDTH, int(y)))
            y += grid_spacing
        
        # Axis labels when zoomed in
        if self.zoom > 3:
            # θ₁ = 0 line (vertical)
            theta1_zero_x = self.pan_x + (0.5 * MAP_RESOLUTION * self.zoom)
            if 0 <= theta1_zero_x <= MAP_WIDTH:
                pygame.draw.line(self.screen, ACCENT_BLUE, (int(theta1_zero_x), 100), (int(theta1_zero_x), MAP_HEIGHT), 2)
            
            # θ₂ = 0 line (horizontal)
            theta2_zero_y = 100 + self.pan_y + (0.5 * MAP_RESOLUTION * self.zoom)
            if 100 <= theta2_zero_y <= MAP_HEIGHT:
                pygame.draw.line(self.screen, ACCENT_BLUE, (0, int(theta2_zero_y)), (MAP_WIDTH, int(theta2_zero_y)), 2)
    
    def draw_color_legend(self):
        # Color legend showing flip time mapping
        legend_rect = pygame.Rect(MAP_WIDTH + 20, 120, PANEL_WIDTH - 40, 80)
        self.draw_panel_section(legend_rect, "Color Legend")
        
        # Legend bar
        legend_bar = pygame.Rect(legend_rect.left + 15, legend_rect.top + 35, 300, 20)
        
        # Draw color gradient
        for i in range(300):
            flip_time = (i / 300) * self.chaos_gen.max_simulation_time
            r, g, b = self.chaos_gen._flip_time_to_color(flip_time)
            pygame.draw.line(self.screen, (r, g, b), 
                           (legend_bar.left + i, legend_bar.top), 
                           (legend_bar.left + i, legend_bar.bottom))
        
        pygame.draw.rect(self.screen, WHITE, legend_bar, 2)
        
        # Legend labels
        labels = ["0s (Fast Flip)", f"{self.chaos_gen.max_simulation_time/2:.1f}s", f"{self.chaos_gen.max_simulation_time:.1f}s+ (Stable)"]
        positions = [legend_bar.left, legend_bar.centerx, legend_bar.right]
        
        for label, pos in zip(labels, positions):
            text_surface = self.font_value.render(label, True, WHITE)
            text_rect = text_surface.get_rect(centerx=pos)
            self.screen.blit(text_surface, (text_rect.x, legend_bar.bottom + 5))
    
    def draw_coordinate_display(self):
        coord_rect = pygame.Rect(MAP_WIDTH + 20, 220, PANEL_WIDTH - 40, 100)
        self.draw_panel_section(coord_rect, "Current Coordinates")
        
        coord_info = [
            ("θ₁", f"{self.current_theta1:.4f} rad", f"{math.degrees(self.current_theta1):.1f}°"),
            ("θ₂", f"{self.current_theta2:.4f} rad", f"{math.degrees(self.current_theta2):.1f}°")
        ]
        
        y_pos = coord_rect.top + 40
        for label, rad_val, deg_val in coord_info:
            # Label
            label_surface = self.font_label.render(f"{label}:", True, WHITE)
            self.screen.blit(label_surface, (coord_rect.left + 15, y_pos))
            
            # Values
            rad_surface = self.font_value.render(rad_val, True, ACCENT_GREEN)
            deg_surface = self.font_value.render(deg_val, True, ACCENT_BLUE)
            
            self.screen.blit(rad_surface, (coord_rect.left + 50, y_pos))
            self.screen.blit(deg_surface, (coord_rect.left + 200, y_pos))
            
            y_pos += 25
        
        # Click instruction
        if not self.is_simulating:
            instruction = "Click on map to start simulation"
            instruction_surface = self.font_value.render(instruction, True, ACCENT_YELLOW)
            self.screen.blit(instruction_surface, (coord_rect.left + 15, y_pos + 10))
    
    def draw_simulation_panel(self):
        sim_rect = pygame.Rect(MAP_WIDTH + 20, 340, PANEL_WIDTH - 40, 320)
        self.draw_panel_section(sim_rect, "Pendulum Simulation")
        
        # Simulation canvas
        canvas_rect = pygame.Rect(sim_rect.left + 15, sim_rect.top + 35, 350, 200)
        pygame.draw.rect(self.screen, BACKGROUND, canvas_rect)
        pygame.draw.rect(self.screen, WHITE, canvas_rect, 2)
        
        if self.is_simulating:
            # Draw pendulum
            center_x = canvas_rect.centerx
            center_y = canvas_rect.top + 40
            scale = 80
            
            # Calculate positions
            x1 = center_x + scale * L1 * math.sin(self.pendulum.theta1)
            y1 = center_y + scale * L1 * math.cos(self.pendulum.theta1)
            x2 = x1 + scale * L2 * math.sin(self.pendulum.theta2)
            y2 = y1 + scale * L2 * math.cos(self.pendulum.theta2)
            
            # Draw trail
            if len(self.pendulum.trail) > 1:
                trail_points = []
                for tx, ty in self.pendulum.trail[-100:]:
                    if canvas_rect.collidepoint(tx, ty):
                        trail_points.append((tx, ty))
                
                for i in range(1, len(trail_points)):
                    alpha = i / len(trail_points)
                    color = (int(ACCENT_GREEN[0] * alpha), int(ACCENT_GREEN[1] * alpha), int(ACCENT_GREEN[2] * alpha))
                    pygame.draw.line(self.screen, color, trail_points[i-1], trail_points[i], max(1, int(alpha * 3)))
            
            # Draw pendulum arms
            pygame.draw.line(self.screen, WHITE, (center_x, center_y), (int(x1), int(y1)), 4)
            pygame.draw.line(self.screen, WHITE, (int(x1), int(y1)), (int(x2), int(y2)), 4)
            
            # Draw masses
            pygame.draw.circle(self.screen, ACCENT_RED, (int(x1), int(y1)), 8)
            pygame.draw.circle(self.screen, ACCENT_BLUE, (int(x2), int(y2)), 10)
            
            # Pivot
            pygame.draw.circle(self.screen, WHITE, (center_x, center_y), 5)
            
            # Current values
            info_y = canvas_rect.bottom + 10
            values = [
                f"θ₁: {math.degrees(self.pendulum.theta1):6.1f}° | ω₁: {self.pendulum.omega1:6.3f} rad/s",
                f"θ₂: {math.degrees(self.pendulum.theta2):6.1f}° | ω₂: {self.pendulum.omega2:6.3f} rad/s",
                f"Energy: {self.pendulum.calculate_energy():8.3f} J | Time: {self.pendulum.time:6.1f} s"
            ]
            
            for i, value in enumerate(values):
                color = [ACCENT_RED, ACCENT_BLUE, ACCENT_GREEN][i]
                value_surface = self.font_value.render(value, True, color)
                self.screen.blit(value_surface, (sim_rect.left + 15, info_y + i * 20))
        else:
            # Instructions
            instructions = [
                "Click anywhere on the chaos map",
                "to start pendulum simulation",
                "",
                "Red areas: Fast chaos (quick flip)",
                "Blue areas: Slower dynamics", 
                "White areas: Very stable motion"
            ]
            
            for i, instruction in enumerate(instructions):
                color = ACCENT_YELLOW if i < 2 else WHITE if instruction else GRAY
                text_surface = self.font_value.render(instruction, True, color)
                text_rect = text_surface.get_rect(centerx=canvas_rect.centerx)
                self.screen.blit(text_surface, (text_rect.x, canvas_rect.top + 30 + i * 25))
    
    def draw_energy_plot(self):
        energy_rect = pygame.Rect(MAP_WIDTH + 20, 680, PANEL_WIDTH - 40, 120)
        self.draw_panel_section(energy_rect, "Energy Analysis")
        
        # Energy plot canvas
        plot_rect = pygame.Rect(energy_rect.left + 15, energy_rect.top + 35, 350, 60)
        pygame.draw.rect(self.screen, BACKGROUND, plot_rect)
        pygame.draw.rect(self.screen, WHITE, plot_rect, 1)
        
        if len(self.pendulum.energy_history) > 1:
            # Find energy range for scaling
            min_energy = min(self.pendulum.energy_history)
            max_energy = max(self.pendulum.energy_history)
            energy_range = max(0.1, max_energy - min_energy)
            
            # Draw energy plot
            points = []
            for i, energy in enumerate(self.pendulum.energy_history):
                x = plot_rect.left + (i / len(self.pendulum.energy_history)) * plot_rect.width
                y = plot_rect.bottom - ((energy - min_energy) / energy_range) * plot_rect.height
                points.append((x, y))
            
            if len(points) > 1:
                pygame.draw.lines(self.screen, ACCENT_GREEN, False, points, 2)
            
            # Energy labels
            current_energy = self.pendulum.energy_history[-1] if self.pendulum.energy_history else 0
            energy_text = f"Current: {current_energy:.3f} J"
            energy_surface = self.font_value.render(energy_text, True, ACCENT_GREEN)
            self.screen.blit(energy_surface, (energy_rect.left + 15, plot_rect.bottom + 10))
    
    def draw_panel_section(self, rect: pygame.Rect, title: str):
        # Section background
        pygame.draw.rect(self.screen, PANEL_BG, rect)
        pygame.draw.rect(self.screen, ACCENT_BLUE, rect, 2)
        
        # Title background
        title_rect = pygame.Rect(rect.left, rect.top, rect.width, 30)
        pygame.draw.rect(self.screen, DARK_GRAY, title_rect)
        
        # Title text
        title_surface = self.font_subtitle.render(title, True, WHITE)
        self.screen.blit(title_surface, (rect.left + 15, rect.top + 5))
    
    def draw_instructions_panel(self):
        instr_rect = pygame.Rect(MAP_WIDTH + 20, 20, PANEL_WIDTH - 40, 80)
        self.draw_panel_section(instr_rect, "Controls")
        
        instructions = [
            "Left Click: Start simulation at point",
            "Right Click: Stop simulation", 
            "Mouse Wheel: Zoom in/out",
            "Drag: Pan around map"
        ]
        
        for i, instruction in enumerate(instructions):
            text_surface = self.font_value.render(instruction, True, WHITE)
            self.screen.blit(text_surface, (instr_rect.left + 15, instr_rect.top + 35 + i * 16))
    
    def run(self):
        running = True
        
        while running:
            running = self.handle_events()
            self.update_simulation()
            
            # Clear screen
            self.screen.fill(BACKGROUND)
            
            # Draw all components
            self.draw_professional_toolbar()
            self.draw_chaos_map()
            
            # Right panel
            panel_rect = pygame.Rect(MAP_WIDTH, 0, PANEL_WIDTH, WINDOW_HEIGHT)
            pygame.draw.rect(self.screen, PANEL_BG, panel_rect)
            pygame.draw.line(self.screen, ACCENT_BLUE, (MAP_WIDTH, 0), (MAP_WIDTH, WINDOW_HEIGHT), 3)
            
            self.draw_instructions_panel()
            self.draw_color_legend()
            self.draw_coordinate_display()
            self.draw_simulation_panel()
            self.draw_energy_plot()
            
            # Status
            if self.chaos_gen.is_generating:
                status_text = f"Generating chaos map... {int(self.chaos_gen.progress * 100)}%"
                status_surface = self.font_label.render(status_text, True, ACCENT_YELLOW)
                self.screen.blit(status_surface, (20, MAP_HEIGHT - 30))
            
            pygame.display.flip()
            self.clock.tick(60)
        
        # Cleanup
        self.audio.stop_audio()
        pygame.quit()

def main():
    """
    Double Pendulum Chaos Map Explorer
    
    This simulation creates a fractal map showing the basin of attraction
    for a double pendulum system. Each pixel represents different initial
    conditions (θ₁, θ₂) and is colored based on how quickly the system
    becomes chaotic (when either pendulum flips over).
    
    The resulting fractal patterns reveal the sensitive dependence on
    initial conditions that characterizes chaotic systems.
    """
    print("Starting Double Pendulum Chaos Map Explorer...")
    print("Generating initial chaos map - this may take a moment...")
    
    explorer = DoublePendulumChaosExplorer()
    explorer.run()

if __name__ == "__main__":
    main()