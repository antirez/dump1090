CREATE DATABASE dump1090;

GRANT ALL PRIVILEGES
ON dump1090.*
TO 'pi'@'localhost'
IDENTIFIED BY 'raspberry'
WITH GRANT OPTION;

GRANT ALL PRIVILEGES
ON dump1090.*
TO 'pi'@'%'
IDENTIFIED BY 'raspberry'
WITH GRANT OPTION;

USE dump1090;

CREATE TABLE IF NOT EXISTS `tracks` (
  `id` int(11) unsigned NOT NULL AUTO_INCREMENT,
  `icao` varchar(6) COLLATE utf8_unicode_ci NOT NULL,
  `alt` int(6) NOT NULL,
  `lat` decimal(17,14) NOT NULL,
  `lon` decimal(17,14) NOT NULL,
  `last_update` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB  DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci AUTO_INCREMENT=1 ;

CREATE TABLE IF NOT EXISTS `flights` (
        `df` smallint(2) NOT NULL,
        `icao` varchar(6) COLLATE utf8_unicode_ci NOT NULL,
        `flight` varchar(7) COLLATE utf8_unicode_ci NOT NULL,
        `squawk` int(4) NOT NULL,
        `regn` varchar(16) COLLATE utf8_unicode_ci NOT NULL,
        `type` varchar(16) COLLATE utf8_unicode_ci NOT NULL,
        `alt` int(6) NOT NULL,
        `vr` smallint(2) NOT NULL,
        `lat` decimal(17,14) NOT NULL,
        `lon` decimal(17,14) NOT NULL,
        `heading` int(6) NOT NULL,
        `speed` int(6) NOT NULL,
        `msgs` int(6) NOT NULL,
        `last_update` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
        UNIQUE KEY (`icao`)
) ENGINE=InnoDB  DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;

