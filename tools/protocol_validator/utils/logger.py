#!/usr/bin/env python3
"""
Logging configuration for UART2ETH protocol validation tool.

Provides structured logging with configurable levels for debugging
and monitoring protocol validation test execution.

References: ADR-010 External Protocol Validation Tool
"""

import logging
import sys
from typing import Optional


def setup_logger(log_level: str = 'info') -> logging.Logger:
    """
    Set up logging with specified level.
    
    Args:
        log_level: Logging level ('debug', 'info', 'warn', 'error')
        
    Returns:
        logging.Logger: Configured logger instance
    """
    # Map string levels to logging constants
    level_map = {
        'debug': logging.DEBUG,
        'info': logging.INFO,
        'warn': logging.WARNING,
        'error': logging.ERROR
    }
    
    level = level_map.get(log_level.lower(), logging.INFO)
    
    # Create logger
    logger = logging.getLogger('uart2eth_validator')
    logger.setLevel(level)
    
    # Remove existing handlers to avoid duplicates
    for handler in logger.handlers[:]:
        logger.removeHandler(handler)
    
    # Create console handler
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(level)
    
    # Create formatter
    formatter = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    console_handler.setFormatter(formatter)
    
    # Add handler to logger
    logger.addHandler(console_handler)
    
    return logger


def get_logger(name: Optional[str] = None) -> logging.Logger:
    """
    Get logger instance.
    
    Args:
        name: Optional logger name (defaults to main validator logger)
        
    Returns:
        logging.Logger: Logger instance
    """
    if name:
        return logging.getLogger(f'uart2eth_validator.{name}')
    else:
        return logging.getLogger('uart2eth_validator')
