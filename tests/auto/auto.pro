TEMPLATE = subdirs

SUBDIRS += geotestplugin \
           positionplugin \
           positionplugintest \
           qgeocodingmanagerplugins \
           qgeoaddress \
           qgeoareamonitor \
           qgeoboundingbox \
           qgeoboundingcircle \
           qgeocodereply \
           qgeocodingmanager \
           qgeocoordinate \
           qgeolocation \
           qgeomaneuver \
           qgeopositioninfo \
           qgeopositioninfosource \
           qgeosatelliteinfo \
           qgeosatelliteinfosource \
           qnmeapositioninfosource \
           qplace \
           qplacecategory \
           qplacecontentrequest \
           qplaceeditorial \
           qplacemanager \
           qplacemanager_nokia \
           qplaceimage \
           qplaceratings \
           qplacereview \
           qplacesearchrequest \
           qplacesupplier \
           qplacesearchresult \
           declarative \
           sphere \
           qmlinterface \
           maptype

contains(config_test_jsondb, yes) {
    SUBDIRS += qplacemanager_jsondb
}

!contains(QT_CONFIG,private_tests):SUBDIRS -= \
    sphere
