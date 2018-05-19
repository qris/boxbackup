pipeline {
  agent none
  stages {
    stage('Configure') {
      parallel {
        stage('Configure on Debian 9 libssl 1.0') {
          agent {
            node {
              label 'debian-9-libssl10'
            }

          }
          steps {
            sh './bootstrap'
            sh './configure'
          }
        }
        stage('Configure on Debian 9 libssl 1.1') {
          agent {
            node {
              label 'debian-9-libssl11'
            }

          }
          steps {
            sh './bootstrap'
            sh './configure'
          }
        }
      }
    }
    stage('Build backup client') {
      parallel {
        stage('Debian 9 libssl 1.0') {
          agent {
            node {
              label 'debian-9-libssl10'
            }

          }
          steps {
            sh 'make build-backup-client'
          }
        }
        stage('Debian 9 libssl 1.1') {
          agent {
            node {
              label 'debian-9-libssl11'
            }

          }
          steps {
            sh 'make build-backup-client'
          }
        }
      }
    }
    stage('Build backup server') {
      parallel {
        stage('Debian 9 libssl 1.0') {
          agent {
            node {
              label 'debian-9-libssl10'
            }

          }
          steps {
            sh 'make build-backup-server'
          }
        }
        stage('Debian 9 libssl 1.1') {
          agent {
            node {
              label 'debian-9-libssl11'
            }

          }
          steps {
            sh 'make build-backup-server'
          }
        }
      }
    }
  }
}