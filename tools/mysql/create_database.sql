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
  `alt` smallint(6) NOT NULL,
  `lat` decimal(17,14) NOT NULL,
  `lon` decimal(17,14) NOT NULL,
  `last_update` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB  DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci AUTO_INCREMENT=1 ;

