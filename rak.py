#!/usr/bin/env python3
"""
MCPE Server Stress Test - PocketMine MP 0.15.10
Protocolo RakNet específico
SOLO para testing autorizado
"""

import socket
import struct
import time
import threading
import random
from datetime import datetime

class MCPEStressTester:
    def __init__(self, server_ip, port=19132):
        self.server_ip = server_ip
        self.port = port
        self.active_connections = 0
        self.max_connections = 0
        self.running = True
        
        # Constantes específicas de RakNet 0.15.10
        self.RAKNET_MAGIC = b"\x00\xff\xff\x00\xfe\xfe\xfe\xfe\xfd\xfd\xfd\xfd\x12\x34\x56\x78"
        self.PACKET_UNCONNECTED_PING = b"\x01"
        self.PACKET_OPEN_CONNECTION_REQUEST_1 = b"\x05"
        
    def create_raknet_handshake(self):
        """Crea paquete de handshake RakNet para 0.15.10"""
        packet = self.PACKET_OPEN_CONNECTION_REQUEST_1
        packet += self.RAKNET_MAGIC
        packet += b"\x00"  # Protocol version (0 = 0.15.x)
        packet += b"\x00" * 1466  # MTU size
        return packet
    
    def create_fake_player(self, player_id):
        """Crea paquete de jugador falso"""
        # Packet ID para Login (0x82 en 0.15.10)
        packet = b"\x82"
        
        # Client UUID (random)
        packet += struct.pack(">Q", random.randint(0, 2**64-1))
        
        # Username
        username = f"StressTest_{player_id:04d}"
        packet += struct.pack(">H", len(username))
        packet += username.encode()
        
        # Protocol version (81 = 0.15.x)
        packet += struct.pack(">I", 81)
        
        # Client ID (random)
        packet += struct.pack(">Q", random.randint(0, 2**64-1))
        
        # Server Address (fake)
        packet += b"\x04"  # IP type (IPv4)
        packet += socket.inet_aton("192.168.1.1")
        packet += struct.pack(">H", 19132)
        
        # Client port
        packet += struct.pack(">H", random.randint(49152, 65535))
        
        return packet
    
    def bot_connection(self, bot_id, duration=30):
        """Simula un jugador/bot conectándose"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(5)
            
            # 1. Enviar ping inicial
            ping_packet = self.PACKET_UNCONNECTED_PING
            ping_packet += struct.pack(">Q", int(time.time() * 1000))
            ping_packet += self.RAKNET_MAGIC
            
            sock.sendto(ping_packet, (self.server_ip, self.port))
            
            # 2. Enviar handshake
            handshake = self.create_raknet_handshake()
            sock.sendto(handshake, (self.server_ip, self.port))
            
            # 3. Intentar login (como jugador)
            login_packet = self.create_fake_player(bot_id)
            sock.sendto(login_packet, (self.server_ip, self.port))
            
            self.active_connections += 1
            if self.active_connections > self.max_connections:
                self.max_connections = self.active_connections
            
            print(f"🤖 Bot {bot_id:04d} conectado. Activos: {self.active_connections}")
            
            # Mantener conexión viva
            start_time = time.time()
            while time.time() - start_time < duration and self.running:
                # Enviar keep-alive cada 5 segundos
                keepalive = b"\x00"  # Packet ID 0x00 = Connected Ping
                keepalive += struct.pack(">Q", int(time.time() * 1000))
                
                sock.sendto(keepalive, (self.server_ip, self.port))
                time.sleep(5)
                
            sock.close()
            
        except Exception as e:
            print(f"❌ Bot {bot_id:04d} error: {e}")
        finally:
            self.active_connections -= 1
    
    def start_stress_test(self, bot_count=50, bot_duration=60, ramp_up=1.0):
        """Inicia prueba de estrés gradual"""
        print(f"🎮 Iniciando Stress Test MCPE 0.15.10")
        print(f"🎯 Servidor: {self.server_ip}:{self.port}")
        print(f"🤖 Bots: {bot_count}")
        print(f"⏱️  Duración por bot: {bot_duration}s")
        print(f"📈 Ramp-up: {ramp_up}s entre bots")
        print("="*50)
        
        bots = []
        
        for i in range(bot_count):
            if not self.running:
                break
                
            bot = threading.Thread(
                target=self.bot_connection,
                args=(i, bot_duration)
            )
            bot.daemon = True
            bot.start()
            bots.append(bot)
            
            print(f"🚀 Lanzando bot {i+1}/{bot_count}")
            time.sleep(ramp_up)
        
        # Esperar a que termine la prueba principal
        time.sleep(bot_duration + 10)
        
        print(f"\n📊 RESULTADOS:")
        print(f"   Máximo concurrente alcanzado: {self.max_connections}")
        print(f"   Bots intentados: {bot_count}")
        
        # Intentar sobrecarga real
        if self.max_connections < 100:
            print(f"\n⚠️  El servidor aguantó {self.max_connections} bots")
            print(f"   Intentando encontrar límite real...")
            self.find_real_limit()
        
        return self.max_connections
    
    def find_real_limit(self):
        """Encuentra el límite real del servidor"""
        print("\n🔍 Buscando límite exacto...")
        
        # Probar con incrementos más grandes
        for target in [100, 150, 200, 250, 300, 500]:
            if not self.running:
                break
                
            print(f"\n🎯 Probando con {target} bots simultáneos...")
            
            threads = []
            self.active_connections = 0
            
            for i in range(target):
                bot = threading.Thread(
                    target=self.bot_connection,
                    args=(i, 10)  # 10 segundos solo
                )
                bot.daemon = True
                bot.start()
                threads.append(bot)
                
                # Lanzar todos rápidamente
                time.sleep(0.01)
            
            # Esperar 15 segundos
            time.sleep(15)
            
            print(f"   Conexiones mantenidas: {self.active_connections}/{target}")
            
            # Si no mantiene al menos 90%, encontramos límite
            if self.active_connections < target * 0.9:
                print(f"🚨 LÍMITE ENCONTRADO: ~{self.active_connections} conexiones")
                break
            
            time.sleep(5)

if __name__ == "__main__":
    print("="*60)
    print("MCPE 0.15.10 STRESS TESTER - POCKETMINE MP")
    print("="*60)
    print("⚠️  SOLO USAR CON AUTORIZACIÓN DEL DUEÑO")
    print("="*60)
    
    import sys
    
    if len(sys.argv) < 2:
        print("Uso: python3 mcpe_stress.py <ip_servidor> [puerto]")
        print("Ejemplo: python3 mcpe_stress.py 192.168.1.100 19132")
        sys.exit(1)
    
    server_ip = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 19132
    
    # Verificación ética
    response = input("\n¿Tienes autorización POR ESCRITO del dueño? (si/no): ")
    if response.lower() != 'si':
        print("❌ Abortando - Sin autorización válida")
        sys.exit(1)
    
    # Configurar prueba
    print("\n⚙️  Configuración de prueba:")
    bots = int(input("   Número de bots (recomendado: 50-200): ") or "50")
    duration = int(input("   Duración por bot segundos (30-120): ") or "60")
    ramp = float(input("   Tiempo entre bots segundos (0.1-2.0): ") or "0.5")
    
    print(f"\n🎮 Iniciando en 5 segundos... Ctrl+C para cancelar")
    time.sleep(5)
    
    tester = MCPEStressTester(server_ip, port)
    
    try:
        max_connections = tester.start_stress_test(bots, duration, ramp)
        
        print(f"\n✅ Prueba completada")
        print(f"📈 Capacidad estimada del servidor: {max_connections} jugadores")
        
        # Recomendaciones basadas en resultados
        if max_connections < 50:
            print("\n💡 RECOMENDACIONES:")
            print("   - Servidor muy limitado (posible VPS pequeña)")
            print("   - Revisar configuración de PocketMine (memory-limit)")
            print("   - Considerar upgrade de recursos")
        elif max_connections < 100:
            print("\n💡 RECOMENDACIONES:")
            print("   - Servidor estándar para 50-80 jugadores")
            print("   - Optimizar plugins y view-distance")
            print("   - Usar AsyncTask para operaciones pesadas")
        else:
            print("\n💡 RECOMENDACIONES:")
            print("   - Servidor robusto")
            print("   - Puede manejar 100+ jugadores")
            print("   - Considerar balanceador si necesita más")
            
    except KeyboardInterrupt:
        print("\n🛑 Prueba cancelada por usuario")
        tester.running = False
    except Exception as e:
        print(f"\n❌ Error: {e}")
