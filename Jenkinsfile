pipeline {
  agent any
  stages {
    stage('Configure') {
      steps {
        sh '''./bootstrap
./configure'''
      }
    }
    stage('Build backup client') {
      steps {
        sh 'make build-backup-client'
      }
    }
    stage('Build backup server') {
      steps {
        sh 'make build-backup-server'
      }
    }
  }
}