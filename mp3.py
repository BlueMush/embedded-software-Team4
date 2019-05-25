import pygame
import time

pygame.mixer.init()

bang = pygame.mixer.Sound("coin.wav")
while True:
	bang.play()
	time.sleep(2.0)



