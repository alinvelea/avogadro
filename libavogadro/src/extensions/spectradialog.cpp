/**********************************************************************
  SpectraDialog - Visualize spectral data from QM calculations

  Copyright (C) 2009 by David Lonie

  This file is part of the Avogadro molecular editor project.
  For more information, see <http://avogadro.openmolecules.net/>

  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU Library General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 ***********************************************************************/

#include "spectradialog.h"

#include <QPen>
#include <QColor>
#include <QColorDialog>
#include <QButtonGroup>
#include <QDebug>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFontDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QPixmap>
#include <QSettings>
#include <QListWidgetItem>

#include <avogadro/molecule.h>
#include <avogadro/plotwidget.h>
#include <openbabel/mol.h>
#include <openbabel/generic.h>

using namespace OpenBabel;
using namespace std;

namespace Avogadro {

  SpectraDialog::SpectraDialog( QWidget *parent, Qt::WindowFlags f ) : 
      QDialog( parent, f )
  {
    ui.setupUi(this);

    // Initialize vars
    m_yaxis = ui.combo_yaxis->currentText();

    // Hide advanced options initially
    ui.gb_customize->hide();

    // setting the limits for the plot
    // TODO: Make persistent
    ui.plot->setDefaultLimits( 4000.0, 400.0, 0.0, 100.0 );
    ui.plot->setAntialiasing(true);
    ui.plot->axis(PlotWidget::BottomAxis)->setLabel(tr("Wavenumber (cm<sup>-1</sup>)"));
    ui.plot->axis(PlotWidget::LeftAxis)->setLabel(m_yaxis);
    m_calculatedSpectra = new PlotObject (Qt::red, PlotObject::Lines, 2);
    m_importedSpectra = new PlotObject (Qt::white, PlotObject::Lines, 2);
    m_nullSpectra = new PlotObject (Qt::white, PlotObject::Lines, 2); // Used to replace disabled plot objects
    ui.plot->addPlotObject(m_calculatedSpectra);
    ui.plot->addPlotObject(m_importedSpectra);

    connect(ui.push_newScheme, SIGNAL(clicked()),
            this, SLOT(addScheme()));
.    connect(ui.push_renameScheme, SIGNAL(clicked()),
            this, SLOT(renameScheme()));
    connect(ui.push_removeScheme, SIGNAL(clicked()),
            this, SLOT(removeScheme()));
    connect(ui.push_colorBackground, SIGNAL(clicked()),
            this, SLOT(changeBackgroundColor()));
    connect(ui.push_colorForeground, SIGNAL(clicked()),
            this, SLOT(changeForegroundColor()));
    connect(ui.push_colorCalculated, SIGNAL(clicked()),
            this, SLOT(changeCalculatedSpectraColor()));
    connect(ui.push_colorImported, SIGNAL(clicked()),
            this, SLOT(changeImportedSpectraColor()));
    connect(ui.push_font, SIGNAL(clicked()),
            this, SLOT(changeFont()));
    connect(ui.push_customize, SIGNAL(clicked()),
            this, SLOT(toggleCustomize()));
    connect(ui.push_save, SIGNAL(clicked()),
            this, SLOT(saveImage()));
    connect(ui.cb_import, SIGNAL(toggled(bool)),
            this, SLOT(toggleImported(bool)));
    connect(ui.cb_calculate, SIGNAL(toggled(bool)),
            this, SLOT(toggleCalculated(bool)));
    connect(ui.cb_labelPeaks, SIGNAL(toggled(bool)),
            this, SLOT(regenerateCalculatedSpectra()));
    connect(ui.push_import, SIGNAL(clicked()),
            this, SLOT(importSpectra()));
    connect(this, SIGNAL(scaleUpdated()),
            this, SLOT(regenerateCalculatedSpectra()));
    connect(ui.spin_scale, SIGNAL(valueChanged(double)),
            this, SLOT(setScale(double)));
    connect(ui.spin_FWHM, SIGNAL(valueChanged(double)),
            this, SLOT(regenerateCalculatedSpectra()));
    connect(ui.combo_yaxis, SIGNAL(currentIndexChanged(QString)),
            this, SLOT(updateYAxis(QString)));
    connect(ui.list_schemes, SIGNAL(currentRowChanged(int)),
            this, SLOT(updateScheme(int)));

    readSettings();
  }

  SpectraDialog::~SpectraDialog()
  {
    writeSettings();
  }

  void SpectraDialog::updateYAxis(QString text)
  {
    if (m_yaxis == ui.combo_yaxis->currentText()) {
      return;
    }
    ui.plot->axis(PlotWidget::LeftAxis)->setLabel(text);
    m_yaxis = text;
    regenerateCalculatedSpectra();
    regenerateImportedSpectra();
  }

  void SpectraDialog::updateScheme(int scheme) {
    ui.list_schemes->setCurrentRow(scheme);
    if (m_scheme != scheme) {
      m_scheme = scheme;
      schemeChanged();
    }
  }

  void SpectraDialog::addScheme() {
    QHash<QString, QVariant> newScheme = schemes->at(m_scheme);
    newScheme["name"] = tr("New Scheme");
    new QListWidgetItem(newScheme["name"].toString(), ui.list_schemes);
    schemes->append(newScheme);
    schemeChanged();
  }

  void SpectraDialog::removeScheme() {
    if (schemes->size() <= 1) return; // Don't delete the last scheme!
    int ret = QMessageBox::question(this, tr("Confirm Scheme Removal"), tr("Really remove current scheme?"));
    if (ret == QMessageBox::Ok) {
      schemes->removeAt(m_scheme);
      delete (ui.list_schemes->takeItem(m_scheme));
    }
  }

  void SpectraDialog::renameScheme() {
    int idx = m_scheme;
    bool ok;
    QString text = QInputDialog::getText(this, tr("Change Scheme Name"),
                                         tr("Enter new name for current scheme:"), QLineEdit::Normal,
                                         schemes->at(m_scheme)["name"].toString(), &ok);
    if (ok) {
      (*schemes)[idx]["name"] = text;
      delete (ui.list_schemes->takeItem(idx));
      ui.list_schemes->insertItem(idx, schemes->at(idx)["name"].toString());
      updateScheme(idx);
    }
  }

  void SpectraDialog::changeBackgroundColor()
  {
    QColor current (schemes->at(m_scheme)["backgroundColor"].value<QColor>());
    QColor color = QColorDialog::getColor(current, this);//, tr("Select Background Color")); <-- Title not supported until Qt 4.5 bump.
    if (color.isValid() && color != current) {
      (*schemes)[m_scheme]["backgroundColor"] = color;
      schemeChanged();
    }
  }

  void SpectraDialog::changeForegroundColor()
  {
    QColor current (schemes->at(m_scheme)["foregroundColor"].value<QColor>());
    QColor color = QColorDialog::getColor(current, this);//, tr("Select Foreground Color")); <-- Title not supported until Qt 4.5 bump.
    if (color.isValid() && color != current) {
      (*schemes)[m_scheme]["foregroundColor"] = color;
      schemeChanged();
    }
  }

  void SpectraDialog::changeCalculatedSpectraColor()
  {
    QColor current (schemes->at(m_scheme)["calculatedColor"].value<QColor>());
    QColor color = QColorDialog::getColor(current, this);//, tr("Select Calculated Spectra Color")); <-- Title not supported until Qt 4.5 bump.
    if (color.isValid() && color != current) {
      (*schemes)[m_scheme]["calculatedColor"] = color;
      schemeChanged();
    }
  }

  void SpectraDialog::changeImportedSpectraColor()
  {
    QColor current (schemes->at(m_scheme)["importedColor"].value<QColor>());
    QColor color = QColorDialog::getColor(current, this);//, tr("Select Imported Spectra Color")); <-- Title not supported until Qt 4.5 bump.
    if (color.isValid() && color != current) {
      (*schemes)[m_scheme]["importedColor"] = color;
      schemeChanged();
    }
  }

  void SpectraDialog::changeFont()
  {
    bool ok;
    QFont current (schemes->at(m_scheme)["font"].value<QFont>());
    QFont font = QFontDialog::getFont(&ok, current, this);
    if (ok) {
      (*schemes)[m_scheme]["font"] = font;
      schemeChanged();
    } 
  }

  void SpectraDialog::setMolecule(Molecule *molecule)
  {
    m_molecule = molecule;
    OBMol obmol = m_molecule->OBMol();

    // Get intensities
    m_vibrations = static_cast<OBVibrationData*>(obmol.GetData(OBGenericDataType::VibrationData));
    if (m_vibrations) {
      //: Choice in a combo box to select infrared spectra
      ui.combo_spectra->addItem(tr("Infrared"));
    }

    // Remove/change this when other methods are added
    if (!m_vibrations) {
      QMessageBox::warning(this, tr("Spectra Visualization"), tr("No supported spectroscopic data present. Please file a bug report (with the molecule file attached) at http://avogadro.openmolecules.net if you believe this is an error."));
      qWarning() << "SpectraDialog::setMolecule: No vibrations to plot!";
      ui.combo_spectra->addItem(tr("No data"));
      return;
    }

    // OK, we have valid vibrations, so store them
    m_wavenumbers = m_vibrations->GetFrequencies();
    vector<double> intensities = m_vibrations->GetIntensities();

    // FIXME: dlonie: remove this when OB is fixed!! Hack to get around bug in how open babel reads in QChem files
    // While openbabel is broken, remove indicies (n+3), where
    // n=0,1,2...
    if (m_wavenumbers.size() == 0.75 * intensities.size()) {
      uint count = 0;
      for (uint i = 0; i < intensities.size(); i++) {
        if ((i+count)%3 == 0){
          intensities.erase(intensities.begin()+i);
          count++;
          i--;
        }
      }
    }

    // Normalize intensities into transmittances
    double maxIntensity=0;
    for (unsigned int i = 0; i < intensities.size(); i++) {
      if (intensities.at(i) >= maxIntensity) {
        maxIntensity = intensities.at(i);
      }
    }

    if (maxIntensity == 0) {
      qWarning() << "SpectraDialog::setMolecule: No intensities > 0 in dataset.";
      return;
    }

    for (unsigned int i = 0; i < intensities.size(); i++) {
      double t = intensities.at(i);
      t = t / maxIntensity; 	// Normalize
      t = 0.97 * t;		// Keeps the peaks from extending to the limits of the plot
      t = 1 - t; 		// Simulate transmittance
      t *= 100;			// Convert to percent
      m_transmittances.push_back(t);
    }

    regenerateCalculatedSpectra();
  }

  void SpectraDialog::writeSettings() const {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    settings.setValue("spectra/scale", m_scale);
    settings.setValue("spectra/currentScheme", m_scheme);
    settings.setValue("spectra/gaussianWidth",ui.spin_FWHM->value());
    settings.setValue("spectra/labelPeaks",ui.cb_labelPeaks->isChecked());
    settings.beginWriteArray("spectra/schemes");
    for (int i = 0; i < schemes->size(); ++i) {
      settings.setArrayIndex(i);
      settings.setValue("scheme", schemes->at(i));
    }
    settings.endArray();
  }

  void SpectraDialog::readSettings() {
    QSettings settings; // Already set up in avogadro/src/main.cpp
    setScale(settings.value("spectra/scale", 1.0).toDouble());
    int scheme = settings.value("spectra/currentScheme", 0).toInt();
    ui.spin_FWHM->setValue(settings.value("spectra/gaussianWidth",0.0).toDouble());
    ui.cb_labelPeaks->setChecked(settings.value("spectra/labelPeaks",false).toBool());
    int size = settings.beginReadArray("spectra/schemes");
    schemes = new QList<QHash<QString, QVariant> >;
    for (int i = 0; i < size; ++i) {
      settings.setArrayIndex(i);
      schemes->append(settings.value("scheme").toHash());
      new QListWidgetItem(schemes->at(i)["name"].toString(), ui.list_schemes);
    }
    settings.endArray();

    // create scheme list if it doesn't already exist
    if (size == 0) {
      // dark
      QHash<QString, QVariant> dark;
      dark["name"] = tr("Dark");
      dark["backgroundColor"] = Qt::black;
      dark["foregroundColor"] = Qt::white;
      dark["calculatedColor"] = Qt::red;
      dark["importedColor"] = Qt::gray;
      dark["font"] = QFont();
      new QListWidgetItem(dark["name"].toString(), ui.list_schemes);
      schemes->append(dark);

      // light
      QHash<QString, QVariant> light;
      light["name"] = tr("Light");
      light["backgroundColor"] = Qt::white;
      light["foregroundColor"] = Qt::black;
      light["calculatedColor"] = Qt::red;
      light["importedColor"] = Qt::gray;
      light["font"] = QFont();
      new QListWidgetItem(dark["name"].toString(), ui.list_schemes);
      schemes->append(light);

    }
    updateScheme(scheme);
  }

  void SpectraDialog::schemeChanged() {
    ui.plot->setBackgroundColor(schemes->at(m_scheme)["backgroundColor"].value<QColor>());
    ui.plot->setForegroundColor(schemes->at(m_scheme)["foregroundColor"].value<QColor>());
    ui.plot->setFont(schemes->at(m_scheme)["font"].value<QFont>());

    QPen currentPen (m_importedSpectra->linePen());
    currentPen.setColor(schemes->at(m_scheme)["importedColor"].value<QColor>());
    m_importedSpectra->setLinePen(currentPen);

    currentPen = (m_calculatedSpectra->linePen());
    currentPen.setColor(schemes->at(m_scheme)["calculatedColor"].value<QColor>());
    m_calculatedSpectra->setLinePen(currentPen);

  }

  void SpectraDialog::setScale(double scale)
  {
    if (scale == m_scale) {
      return;
    }
    m_scale = scale;
    ui.spin_scale->setValue(scale);
    emit scaleUpdated();
  }

  void SpectraDialog::importSpectra()
  {
    QFileInfo defaultFile(m_molecule->fileName());
    QString defaultPath = defaultFile.canonicalPath();
    if (defaultPath.isEmpty()) {
      defaultPath = QDir::homePath();
    }

    QString defaultFileName = defaultPath + '/' + defaultFile.baseName() + ".tsv";
    QString filename 	= QFileDialog::getOpenFileName(this, tr("Import Spectra"), defaultFileName, tr("Tab Separated Values (*.tsv);;Comma Separated Values (*.csv);;JCAMP-DX (*.jdx);;All Files (* *.*)"));

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      qWarning() << "Error reading file " << filename;
      return;
    }
    
    // get file extension
    QStringList tmp 	= filename.split(".");
    QString ext 	= tmp.at(tmp.size()-1);
    
    // Clear out any old data
    m_imported_wavenumbers.clear();
    m_imported_transmittances.clear();

    QTextStream in(&file);

    if (!ext.compare("tsv") || !ext.compare("TSV")) {
      while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().startsWith("#")) continue; 	//discard comments
        QStringList data = line.split("\t");
        if (data.at(0).toDouble() && data.at(1).toDouble()) {
          m_imported_wavenumbers.push_back(data.at(0).toDouble());
          m_imported_transmittances.push_back(data.at(1).toDouble());
        }
        else {
          qWarning() << "SpectraDialog::importSpectra Skipping entry as invalid:\n\tWavenumber: " << data.at(0) << "\n\tTransmittance: " << data.at(1);
        }
      }
    }
    else if (!ext.compare("csv") || !ext.compare("CSV")) {
      while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().startsWith("#")) continue; 	//discard comments
        QStringList data = line.split(",");
        if (data.at(0).toDouble() && data.at(1).toDouble()) {
          m_imported_wavenumbers.push_back(data.at(0).toDouble());
          m_imported_transmittances.push_back(data.at(1).toDouble());
        }
        else {
          qWarning() << "SpectraDialog::importSpectra Skipping entry as invalid:\n\tWavenumber: " << data.at(0) << "\n\tTransmittance: " << data.at(1);
        }
      }
    }
    else if (!ext.compare("jdx") || !ext.compare("JDX")) {
      while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().startsWith("#")) continue; 	//discard comments
        QStringList data = line.split(QRegExp("\\s+")); //regex finds whitespace
        if (data.at(0).toDouble() && data.at(1).toDouble()) {
          m_imported_wavenumbers.push_back(data.at(0).toDouble());
          m_imported_transmittances.push_back(data.at(1).toDouble());
        }
        else {
          qWarning() << "SpectraDialog::importSpectra Skipping entry as invalid:\n\tWavenumber: " << data.at(0) << "\n\tTransmittance: " << data.at(1);
        }
      }
    }
    else {
      QMessageBox::warning(this, tr("Spectra Import"), tr("Unknown extension: %1").arg(ext));
      return;
    }  

    // Check to see if the transmittances are in fractions or percents by looking for any transmittances > 1.5
    bool convert = true;
    for (uint i = 0; i < m_imported_transmittances.size(); i++) {
      if (m_imported_transmittances.at(i) > 1.5) { // If transmittances exist greater than this, they're already in percent.
        convert = false;
        break;
      }
    }
    if (convert) {
      for (uint i = 0; i < m_imported_transmittances.size(); i++) {
        m_imported_transmittances.at(i) *= 100;
      }
    }

    ui.push_colorImported->setEnabled(true);
    ui.cb_import->setEnabled(true);
    ui.cb_import->setChecked(true);
    getImportedSpectra(m_importedSpectra);
    updatePlot();
  }

  void SpectraDialog::saveImage()
  {
    QFileInfo defaultFile(m_molecule->fileName());
    QString defaultPath = defaultFile.canonicalPath();
    if (defaultPath.isEmpty())
      defaultPath = QDir::homePath();

    QString defaultFileName = defaultPath + '/' + defaultFile.baseName() + ".png";
    QString filename 	= QFileDialog::getSaveFileName(this, tr("Save Spectra"), defaultFileName, tr("png (*.png);;jpg (*.jpg);;bmp (*.bmp);;tiff (*.tiff);;All Files (*.*)"));
    QPixmap pix = QPixmap::grabWidget(ui.plot);
    if (!pix.save(filename)) {
      qWarning() << "SpectraDialog::saveImage Error saving plot to " << filename;
    }
  }

  void SpectraDialog::toggleImported(bool state) {
    if (state) {
      ui.plot->replacePlotObject(1,m_importedSpectra);
    }
    else {
      ui.plot->replacePlotObject(1,m_nullSpectra);
    }
    updatePlot();
  }

  void SpectraDialog::toggleCalculated(bool state) {
    if (state) {
      ui.plot->replacePlotObject(0,m_calculatedSpectra);
    }
    else {
      ui.plot->replacePlotObject(0,m_nullSpectra);
    }
    updatePlot();
  }

  void SpectraDialog::toggleCustomize() {
    if (ui.gb_customize->isHidden()) {
      ui.push_customize->setText(tr("Customi&ze <<"));
      ui.gb_customize->show();
    }
    else {
      ui.push_customize->setText(tr("Customi&ze >>"));
      ui.gb_customize->hide();
    }
  }

  void SpectraDialog::regenerateCalculatedSpectra() {
    getCalculatedSpectra(m_calculatedSpectra);
    updatePlot();
  }

  void SpectraDialog::regenerateImportedSpectra() {
    getImportedSpectra(m_importedSpectra);
    updatePlot();
  }

  void SpectraDialog::updatePlot()
  {
    ui.plot->update();
  }

  void SpectraDialog::getCalculatedSpectra(PlotObject *plotObject)
  {
    plotObject->clearPoints();
    if (ui.spin_FWHM->value() == 0.0) {
      getCalculatedSinglets(plotObject);
    } 
    else {
      getCalculatedGaussians(plotObject);
    }
    if (ui.combo_yaxis->currentText() == "Absorbance (%)") {
      for(int i = 0; i< plotObject->points().size(); i++) {
        double absorbance = 100 - plotObject->points().at(i)->y();
        plotObject->points().at(i)->setY(absorbance);
      }
    }
  }

  void SpectraDialog::getCalculatedSinglets(PlotObject *plotObject)
  {
    plotObject->addPoint( 400, 100); // Initial point

    for (uint i = 0; i < m_transmittances.size(); i++) {
      double wavenumber = m_wavenumbers.at(i) * m_scale;
      double transmittance = m_transmittances.at(i);
      plotObject->addPoint ( wavenumber, 100 );
      if (ui.cb_labelPeaks->isChecked()) {
        plotObject->addPoint ( wavenumber, transmittance, QString::number(wavenumber, 'f', 1));
      }
      else {
       	plotObject->addPoint ( wavenumber, transmittance );
      }
      plotObject->addPoint ( wavenumber, 100 );
    }
    plotObject->addPoint( 4000, 100); // Final point
  }


  void SpectraDialog::getCalculatedGaussians(PlotObject *plotObject)
  {
    // convert FWHM to sigma squared
    double FWHM = ui.spin_FWHM->value();
    double s2	= pow( (FWHM / ( 2 * sqrt( 2 * log(2) ) ) ), 2);
    
    // determine range
    // - find maximum and minimum
    double min = 0.0 + 2*FWHM;
    double max = 4000.0 - 2*FWHM;
    for (uint i = 0; i < m_wavenumbers.size(); i++) {
      double cur = m_wavenumbers.at(i);
      if (cur > max) max = cur;
      if (cur < min) min = cur;
    }
    min -= 2*FWHM;
    max += 2*FWHM;      
    // - get resolution (TODO)
    double res = 1.0;
    // create points
    for (double x = min; x < max; x += res) {
      double y = 100;
      for (uint i = 0; i < m_transmittances.size(); i++) {
        double t = m_transmittances.at(i);
        double w = m_wavenumbers.at(i) * m_scale;
        y += (t-100) * exp( - ( pow( (x - w), 2 ) ) / (2 * s2) );
      }
      plotObject->addPoint(x,y);
    }

    // Normalization is probably screwed up, so renormalize the data
    max = plotObject->points().at(0)->y();
    min = max;
    for(int i = 0; i< plotObject->points().size(); i++) {
      double cur = plotObject->points().at(i)->y();
      if (cur < min) min = cur;
      if (cur > max) max = cur;
    }
    for(int i = 0; i< plotObject->points().size(); i++) {
      double cur = plotObject->points().at(i)->y();
      // cur - min 		: Shift lowest point of plot to be at zero
      // 100 / (max - min)	: Conversion factor for current spread -> percent
      // * 0.97 + 3		: makes plot stay away from 0 transmittance 
      //			: (easier to see multiple peaks on strong signals)
      plotObject->points().at(i)->setY( (cur - min) * 100 / (max - min) * 0.97 + 3);
    }    
  }

  void SpectraDialog::getImportedSpectra(PlotObject *plotObject)
  {
    plotObject->clearPoints();
    for (uint i = 0; i < m_imported_transmittances.size(); i++) {
      double wavenumber = m_imported_wavenumbers.at(i);
      double y = m_imported_transmittances.at(i);
      if (ui.combo_yaxis->currentText() == "Absorbance (%)") {
        y = 100 - y;
      }
      plotObject->addPoint ( wavenumber, y );
    }
  }
}

#include "spectradialog.moc"
