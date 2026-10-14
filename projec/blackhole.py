import pygame
import math
import random
import numpy as np

# Initialize Pygame
pygame.init()

# Screen dimensions
WIDTH, HEIGHT = 1600, 1000
screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("GARGANTUA - Interstellar Black Hole")

# Colors inspired by Interstellar
SPACE_BLACK = (0, 0, 0)
ACCRETION_ORANGE = (255, 140, 60)
ACCRETION_YELLOW = (255, 200, 80)
ACCRETION_WHITE = (255, 240, 200)
ACCRETION_BLUE = (200, 220, 255)
LENSING_GLOW = (255, 180, 100)
PHOTON_RING = (255, 220, 150)

# Fonts
font = pygame.font.Font(None, 16)
title_font = pygame.font.Font(None, 28)

# Physical constants (scaled for visual effect)
BLACK_HOLE_MASS = 1.0
EVENT_HORIZON_RADIUS = 30
PHOTON_SPHERE_RADIUS = 45
ISCO_RADIUS = 90

# Camera system
camera_distance = 600
camera_rot = [0.2, 0.0]
zoom = 1.0
time_factor = 0
simulation_speed = 1.0
rotation_speed = 0.5

# Accretion disk parameters
DISK_INNER_RADIUS = ISCO_RADIUS
DISK_OUTER_RADIUS = 400
DISK_THICKNESS = 30

def gravitational_lensing_factor(radius, mass):
    """Calculate light bending due to gravity"""
    rs = 2.0 * mass * EVENT_HORIZON_RADIUS
    if radius <= rs:
        return 0
    return 1.0 + (1.5 * rs) / radius

def temperature_from_radius(radius):
    """Accretion disk temperature profile - hotter closer to BH"""
    if radius < DISK_INNER_RADIUS:
        return 8000
    temp = 6000 * (DISK_INNER_RADIUS / radius) ** 0.75
    return max(1000, min(8000, temp))

def color_from_temperature(temp):
    """Convert temperature to realistic color"""
    if temp > 6000:
        return ACCRETION_WHITE
    elif temp > 4000:
        return ACCRETION_YELLOW  
    elif temp > 2500:
        return ACCRETION_ORANGE
    else:
        return (255, 100, 50)

def project_3d(x, y, z):
    """3D to 2D projection with relativistic effects"""
    try:
        # Apply camera rotation
        cos_rx, sin_rx = math.cos(camera_rot[0]), math.sin(camera_rot[0])
        cos_ry, sin_ry = math.cos(camera_rot[1]), math.sin(camera_rot[1])
        
        # Rotate around Y axis
        x_rot = x * cos_ry - z * sin_ry
        z_rot = x * sin_ry + z * cos_ry
        
        # Rotate around X axis  
        y_rot = y * cos_rx - z_rot * sin_rx
        z_cam = y * sin_rx + z_rot * cos_rx + camera_distance
        
        if z_cam <= 1:
            return None
        
        # Gravitational lensing effect
        r = math.sqrt(x**2 + y**2 + z**2)
        lensing = gravitational_lensing_factor(r, BLACK_HOLE_MASS)
        
        scale = (400 * zoom * lensing) / z_cam
        screen_x = WIDTH // 2 + x_rot * scale
        screen_y = HEIGHT // 2 - y_rot * scale
        
        return (screen_x, screen_y, scale, z_cam, lensing)
    except:
        return None

class AccretionDiskParticle:
    """Particle in the accretion disk with realistic physics"""
    
    def __init__(self, radius, angle):
        self.radius = radius
        self.angle = angle
        self.orbital_velocity = math.sqrt(BLACK_HOLE_MASS / max(radius, DISK_INNER_RADIUS)) * 20
        
        # Vertical position with some randomness
        self.height = random.uniform(-DISK_THICKNESS/2, DISK_THICKNESS/2)
        
        # Physical properties
        self.temperature = temperature_from_radius(radius)
        self.brightness = 0.5 + (self.temperature / 8000) * 1.5
        self.mass = random.uniform(0.01, 0.1)
        
        # Visual properties
        self.base_color = color_from_temperature(self.temperature)
        self.size = max(1, int(math.sqrt(self.mass) * 3))
        
        # Turbulence
        self.turbulence_phase = random.uniform(0, 2*math.pi)
        
    def update(self, dt):
        """Update particle orbital motion"""
        # Orbital motion (Keplerian)
        angular_velocity = self.orbital_velocity / self.radius
        self.angle += angular_velocity * dt
        
        # Turbulence for realistic motion
        self.turbulence_phase += dt * 5
        turbulence = math.sin(self.turbulence_phase) * 0.1
        self.radius += turbulence
        
        # Gradually spiral inward due to friction
        self.radius *= 0.9999
        
        # Update temperature as we get closer
        self.temperature = temperature_from_radius(self.radius)
        self.base_color = color_from_temperature(self.temperature)
        self.brightness = 0.5 + (self.temperature / 8000) * 1.5
        
        # Remove if too close to event horizon
        return self.radius > EVENT_HORIZON_RADIUS * 1.2
        
    def get_position(self):
        """Get 3D position"""
        x = self.radius * math.cos(self.angle)
        z = self.radius * math.sin(self.angle)
        y = self.height
        return (x, y, z)
    
    def draw(self, screen):
        """Draw particle with relativistic effects"""
        pos = self.get_position()
        proj = project_3d(*pos)
        
        if proj:
            px, py, scale, z_dist, lensing = proj
            
            if 0 <= px < WIDTH and 0 <= py < HEIGHT:
                # Size affected by lensing and distance
                size = max(1, int(self.size * scale * 0.01 * lensing))
                
                # Brightness affected by redshift and lensing
                redshift_factor = max(0.3, 1.0 - (EVENT_HORIZON_RADIUS * 2) / self.radius)
                total_brightness = self.brightness * redshift_factor * lensing
                
                # Apply brightness to color
                color = tuple(max(0, min(255, int(c * total_brightness))) for c in self.base_color)
                
                # Hot inner disk particles get bloom effect
                if self.temperature > 5000 and size > 1:
                    bloom_size = size + 2
                    bloom_color = tuple(max(0, c//4) for c in color)
                    pygame.draw.circle(screen, bloom_color, (int(px), int(py)), bloom_size)
                
                pygame.draw.circle(screen, color, (int(px), int(py)), size)

class PhotonRing:
    """The famous photon ring around the black hole"""
    
    def __init__(self):
        self.particles = []
        self.create_ring()
    
    def create_ring(self):
        """Create photons orbiting at photon sphere"""
        for i in range(180):  # Dense ring
            angle = (i / 180) * 2 * math.pi
            
            # Slight radius variation for realism
            radius = PHOTON_SPHERE_RADIUS * (1 + random.uniform(-0.05, 0.05))
            height = random.uniform(-5, 5)
            
            photon = {
                'radius': radius,
                'angle': angle,
                'height': height,
                'brightness': random.uniform(0.8, 1.2),
                'phase': random.uniform(0, 2*math.pi)
            }
            self.particles.append(photon)
    
    def update(self, dt):
        """Update photon orbits"""
        for photon in self.particles:
            # Photons at photon sphere have specific orbital period
            orbital_velocity = math.sqrt(BLACK_HOLE_MASS / photon['radius']) * 25
            angular_velocity = orbital_velocity / photon['radius']
            photon['angle'] += angular_velocity * dt
            
            # Slight brightness pulsation
            photon['phase'] += dt * 3
            photon['current_brightness'] = photon['brightness'] * (0.8 + 0.4 * math.sin(photon['phase']))
    
    def draw(self, screen):
        """Draw the photon ring"""
        for photon in self.particles:
            x = photon['radius'] * math.cos(photon['angle'])
            z = photon['radius'] * math.sin(photon['angle'])
            y = photon['height']
            
            proj = project_3d(x, y, z)
            if proj:
                px, py, scale, z_dist, lensing = proj
                
                if 0 <= px < WIDTH and 0 <= py < HEIGHT:
                    brightness = photon['current_brightness'] * lensing
                    color = tuple(max(0, min(255, int(c * brightness))) for c in PHOTON_RING)
                    size = max(1, int(scale * 0.02))
                    
                    pygame.draw.circle(screen, color, (int(px), int(py)), size)

class RelativisticJet:
    """Relativistic jets from the black hole poles"""
    
    def __init__(self):
        self.particles = []
        self.activity = 0.0
    
    def update_activity(self, accretion_rate):
        """Jets powered by accretion"""
        target = min(1.0, accretion_rate * 3)
        self.activity += (target - self.activity) * 0.05
        
        # Create jet particles
        if self.activity > 0.3 and random.random() < 0.4:
            self.create_jet_particle()
    
    def create_jet_particle(self):
        """Create relativistic particle"""
        # Jets from poles
        pole = random.choice([-1, 1])
        
        # Position near black hole
        x = random.uniform(-EVENT_HORIZON_RADIUS*0.8, EVENT_HORIZON_RADIUS*0.8)
        y = EVENT_HORIZON_RADIUS * 0.5 * pole
        z = random.uniform(-EVENT_HORIZON_RADIUS*0.8, EVENT_HORIZON_RADIUS*0.8)
        
        # High velocity along jet axis
        velocity = random.uniform(80, 150)
        
        particle = {
            'x': x, 'y': y, 'z': z,
            'vx': random.uniform(-8, 8),
            'vy': velocity * pole,
            'vz': random.uniform(-8, 8),
            'brightness': random.uniform(0.8, 1.5) * self.activity,
            'lifetime': random.uniform(30, 80),
            'age': 0
        }
        
        self.particles.append(particle)
    
    def update(self, dt):
        """Update jet particles"""
        active_particles = []
        
        for p in self.particles:
            p['age'] += dt
            
            if p['age'] < p['lifetime']:
                # Update position
                p['x'] += p['vx'] * dt
                p['y'] += p['vy'] * dt
                p['z'] += p['vz'] * dt
                
                # Fade with age
                age_factor = 1.0 - (p['age'] / p['lifetime'])
                p['current_brightness'] = p['brightness'] * age_factor
                
                active_particles.append(p)
        
        self.particles = active_particles
    
    def draw(self, screen):
        """Draw jets with blue-white color"""
        for p in self.particles:
            proj = project_3d(p['x'], p['y'], p['z'])
            if proj:
                px, py, scale, z_dist, lensing = proj
                
                if 0 <= px < WIDTH and 0 <= py < HEIGHT:
                    brightness = p['current_brightness']
                    color = tuple(max(0, min(255, int(c * brightness))) for c in ACCRETION_BLUE)
                    size = max(1, int(scale * 0.02))
                    
                    pygame.draw.circle(screen, color, (int(px), int(py)), size)

# Initialize systems
accretion_disk = []
photon_ring = PhotonRing()
jets = RelativisticJet()

# Create accretion disk
for _ in range(2000):
    radius = random.uniform(DISK_INNER_RADIUS, DISK_OUTER_RADIUS)
    # More particles closer to black hole (realistic density)
    if random.random() < (DISK_OUTER_RADIUS / radius) * 0.3:
        angle = random.uniform(0, 2 * math.pi)
        particle = AccretionDiskParticle(radius, angle)
        accretion_disk.append(particle)

def draw_event_horizon():
    """Draw the black hole event horizon with lensing"""
    center_x, center_y = WIDTH // 2, HEIGHT // 2
    shadow_radius = int(EVENT_HORIZON_RADIUS * zoom * 400 / camera_distance)
    
    if shadow_radius > 5:
        # Gravitational lensing glow
        for i in range(shadow_radius * 3, shadow_radius, -max(1, shadow_radius//12)):
            glow_factor = 1.0 - (i - shadow_radius) / (shadow_radius * 2)
            intensity = max(0, int(40 * glow_factor * glow_factor))
            
            glow_color = (
                max(0, int(intensity * 1.2)),
                max(0, int(intensity * 0.9)), 
                max(0, int(intensity * 0.6))
            )
            pygame.draw.circle(screen, glow_color, (center_x, center_y), i)
        
        # Event horizon shadow - pure black
        pygame.draw.circle(screen, (0, 0, 0), (center_x, center_y), shadow_radius)
        
        # Subtle edge brightening (light capture)
        if shadow_radius > 8:
            edge_color = (30, 20, 10)
            pygame.draw.circle(screen, edge_color, (center_x, center_y), shadow_radius, 2)

def draw_background_stars():
    """Static background stars"""
    random.seed(42)  # Consistent stars
    for _ in range(200):
        x = random.randint(50, WIDTH-50)
        y = random.randint(50, HEIGHT-50)
        
        # Don't draw stars too close to black hole
        center_x, center_y = WIDTH // 2, HEIGHT // 2
        dist = math.sqrt((x - center_x)**2 + (y - center_y)**2)
        
        shadow_radius = EVENT_HORIZON_RADIUS * zoom * 400 / camera_distance
        if dist > shadow_radius * 2:
            brightness = random.uniform(0.3, 1.0)
            star_color = random.choice([
                (255, 255, 255), (255, 240, 200), (200, 220, 255)
            ])
            color = tuple(max(0, min(255, int(c * brightness))) for c in star_color)
            pygame.draw.circle(screen, color, (x, y), 1)

def draw_ui():
    """Draw minimal UI like in the movie"""
    # Title
    title = title_font.render("G A R G A N T U A", True, (200, 180, 140))
    title_rect = title.get_rect(center=(WIDTH//2, 40))
    screen.blit(title, title_rect)
    
    # Status
    disk_count = len(accretion_disk)
    accretion_rate = min(1.0, disk_count / 1500.0)
    
    info = [
        f"Event Horizon: {EVENT_HORIZON_RADIUS}km",
        f"Photon Sphere: {PHOTON_SPHERE_RADIUS}km", 
        f"Accretion Rate: {accretion_rate:.2f}",
        f"Jet Activity: {jets.activity:.2f}",
        f"Simulation Speed: {simulation_speed:.1f}x",
        "",
        "Mouse: Rotate view",
        "Wheel: Zoom",
        "WASD: Camera",
        "Space: Pause",
        "+/-: Speed"
    ]
    
    y = HEIGHT - 220
    for line in info:
        if line == "":
            y += 10
            continue
        color = (150, 140, 120) if ":" in line else (100, 100, 100)
        text = font.render(line, True, color)
        screen.blit(text, (20, y))
        y += 18

def main():
    global camera_rot, zoom, time_factor, simulation_speed, camera_distance, accretion_disk
    
    clock = pygame.time.Clock()
    running = True
    paused = False
    dragging = False
    
    print("🌌 GARGANTUA - Interstellar Black Hole Simulation")
    print("⚫ Scientifically accurate visualization")
    print("🎬 Inspired by Christopher Nolan's Interstellar")
    
    while running:
        dt = clock.tick(60) / 1000.0
        
        # Handle events
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_SPACE:
                    paused = not paused
                elif event.key in [pygame.K_PLUS, pygame.K_EQUALS]:
                    simulation_speed = min(3.0, simulation_speed * 1.2)
                elif event.key == pygame.K_MINUS:
                    simulation_speed = max(0.1, simulation_speed / 1.2)
                elif event.key == pygame.K_w:
                    camera_distance = max(300, camera_distance - 40)
                elif event.key == pygame.K_s:
                    camera_distance = min(1200, camera_distance + 40)
                elif event.key == pygame.K_a:
                    camera_rot[1] -= 0.15
                elif event.key == pygame.K_d:
                    camera_rot[1] += 0.15
                elif event.key == pygame.K_r:
                    # Reset
                    camera_rot = [0.2, 0.0]
                    zoom = 1.0
                    camera_distance = 600
            
            elif event.type == pygame.MOUSEBUTTONDOWN:
                if event.button == 1:
                    dragging = True
                    pygame.mouse.get_rel()
                elif event.button == 4:  # Scroll up
                    zoom = min(2.5, zoom * 1.1)
                elif event.button == 5:  # Scroll down
                    zoom = max(0.3, zoom / 1.1)
            
            elif event.type == pygame.MOUSEBUTTONUP:
                if event.button == 1:
                    dragging = False
            
            elif event.type == pygame.MOUSEMOTION:
                if dragging:
                    dx, dy = pygame.mouse.get_rel()
                    camera_rot[1] += dx * 0.01
                    camera_rot[0] += dy * 0.01
                    camera_rot[0] = max(-0.8, min(0.8, camera_rot[0]))
        
        # Update simulation
        if not paused:
            time_factor += dt * simulation_speed
            
            # Slow camera auto-rotation for cinematic effect
            camera_rot[1] += rotation_speed * dt * 0.1
            
            # Update accretion disk
            active_particles = []
            for particle in accretion_disk:
                if particle.update(dt * simulation_speed):
                    active_particles.append(particle)
            accretion_disk = active_particles
            
            # Add new particles to maintain disk
            while len(accretion_disk) < 1800:
                radius = random.uniform(DISK_INNER_RADIUS, DISK_OUTER_RADIUS)
                angle = random.uniform(0, 2 * math.pi)
                new_particle = AccretionDiskParticle(radius, angle)
                accretion_disk.append(new_particle)
            
            # Update other systems
            photon_ring.update(dt * simulation_speed)
            
            accretion_rate = len(accretion_disk) / 2000.0
            jets.update_activity(accretion_rate)
            jets.update(dt * simulation_speed)
        
        # Render
        screen.fill(SPACE_BLACK)
        
        # Draw in correct order for proper layering
        draw_background_stars()
        
        # Draw accretion disk (back to front for proper alpha blending)
        disk_particles_with_depth = []
        for particle in accretion_disk:
            pos = particle.get_position()
            proj = project_3d(*pos)
            if proj:
                disk_particles_with_depth.append((particle, proj[3]))  # z_dist
        
        # Sort by depth (far to near)
        disk_particles_with_depth.sort(key=lambda x: x[1], reverse=True)
        
        for particle, _ in disk_particles_with_depth:
            particle.draw(screen)
        
        # Draw jets
        jets.draw(screen)
        
        # Draw photon ring
        photon_ring.draw(screen)
        
        # Draw black hole (always on top)
        draw_event_horizon()
        
        # UI
        draw_ui()
        
        # FPS
        fps = clock.get_fps()
        fps_color = (100, 200, 100) if fps > 50 else (200, 200, 100) if fps > 30 else (200, 100, 100)
        fps_text = font.render(f"FPS: {fps:.0f}", True, fps_color)
        screen.blit(fps_text, (WIDTH - 80, 20))
        
        pygame.display.flip()
    
    pygame.quit()
    print("✅ Gargantua simulation complete!")

if __name__ == "__main__":
    main()          